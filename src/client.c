#include "dshot.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
extern int events_start(bool lid), lid_closed(void), lock_available(void), lock_and_blank(bool should_lock);
extern bool setting_lock(void);
static volatile sig_atomic_t stop_requested;
static void handle_signal(int sig) { stop_requested = sig; }
static void usage(void) {
    puts("Usage: dshot [-dimsu] [-t seconds] [-w pid] [--] [command [args...]]\n"
         "       dshot install | uninstall | status | doctor\n"
         "       dshot config [init] | shell-init [zsh|bash|fish]\n\n"
         "Caffeinate for closed lids. The hold ends with this session.\n"
         "-d display awake (lid open)  -i prevent idle sleep  -m prevent disk idle\n"
         "-s prevent system sleep on AC  -u declare user activity\n"
         "-t timeout in seconds  -w stop when PID exits\n"
         "A command takes precedence over -t and -w, as in caffeinate.\n"
         "Closing the originating terminal ends the hold. tmux detach keeps its pane alive.");
}
static int positive_number(const char *s, unsigned long long *value) {
    char *end; errno = 0; *value = strtoull(s, &end, 0);
    return !*s || *s == '-' || errno || *end || *value > 2147483647ULL ? -1 : 0;
}
static int spawn_command(char **args, pid_t *pid) {
    posix_spawnattr_t attr; posix_spawnattr_init(&attr);
    sigset_t defaults, mask; sigemptyset(&defaults); sigemptyset(&mask);
    sigaddset(&defaults, SIGINT); sigaddset(&defaults, SIGTERM); sigaddset(&defaults, SIGHUP); sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigdefault(&attr, &defaults); posix_spawnattr_setsigmask(&attr, &mask);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    int rc = posix_spawnp(pid, args[0], NULL, &attr, args, environ);
    posix_spawnattr_destroy(&attr); return rc;
}
static pid_t start_assertions(const char *flags, bool closed, unsigned long long timeout) {
    char filtered[16] = "-", pid[32], duration[32]; size_t n = 1;
    for (const char *p = flags; *p; p++) if (!closed || (*p != 'd' && *p != 'u')) filtered[n++] = *p;
    filtered[n] = 0;
    /* A removed display-only assertion must not silently become caffeinate's default idle assertion. */
    if (n == 1) return 0;
    snprintf(pid, sizeof(pid), "%d", getpid());
    snprintf(duration, sizeof(duration), "%llu", timeout);
    char *args[] = {"/usr/bin/caffeinate", filtered, "-w", pid, "-t", duration, NULL};
#ifdef DSHOT_TEST
    /* Unit/integration tests never create real power assertions. */
    (void)args; return 0;
#else
    pid_t child = 0; int error = spawn_command(args, &child);
    if (error) { diagnostic("caffeinate: %s", strerror(error)); return -1; }
    return child;
#endif
}
static void stop_assertions(pid_t *pid) {
    if (*pid > 0) {
        kill(*pid, SIGTERM);
        double end = now_seconds() + 1;
        pid_t done;
        while ((done = waitpid(*pid, NULL, WNOHANG)) == 0 && now_seconds() < end) usleep(10000);
        if (done == 0) { kill(*pid, SIGKILL); waitpid(*pid, NULL, 0); }
    }
    *pid = 0;
}
int client_main(int argc, char **argv) {
    char flags[8] = ""; bool present[256] = {false}; unsigned long long timeout = 0, target = 0;
    optind = 1; opterr = 0; int option;
    while ((option = getopt(argc, argv, "+dimsut:w:h")) != -1) {
        if (strchr("dimsu", option)) present[option] = true;
        else if (option == 't' || option == 'w') {
            unsigned long long value;
            if (positive_number(optarg, &value)) { diagnostic("invalid -%c value", option); return 2; }
            if (option == 't') timeout = value; else target = value;
        } else if (option == 'h') { usage(); return 0; }
        else { usage(); return 2; }
    }
    size_t n = 0; for (const char *p = "dimsu"; *p; p++) if (present[(unsigned char)*p]) flags[n++] = *p;
    if (!n) flags[n++] = 'i'; flags[n] = 0;
    bool command = optind < argc, do_lock = setting_lock();
    if (command) { target = 0; timeout = 0; }
    uint64_t target_birth = target ? process_birth((pid_t)target) : 0;
    if (target && !target_birth) return 0;
    int terminal = open("/dev/tty", O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    /* Do not equate terminal lifetime with parent lifetime: a shell may exec dshot. */
    int fd = connect_service();
    if (fd < 0) { diagnostic("service unavailable; run dshot install first"); if (terminal >= 0) close(terminal); return 1; }
    if (do_lock && !lock_available()) { diagnostic("screen-lock API unavailable; see dshot doctor"); close(fd); return 1; }
    signal(SIGPIPE, SIG_IGN); signal(SIGINT, handle_signal); signal(SIGTERM, handle_signal); signal(SIGHUP, handle_signal);
    char reply[512] = "";
    if (request(fd, "A\n", reply, sizeof(reply)) || strcmp(reply, "OK")) {
        diagnostic("cannot acquire hold: %s", reply[0] ? reply : strerror(errno)); close(fd); return 1;
    }
    int result = 0, closed = lid_closed();
    if (closed < 0) { diagnostic("cannot read lid state"); result = 1; goto release; }
    pid_t assertion = start_assertions(flags, closed, timeout);
    if (assertion < 0) { result = 1; goto release; }
    if (closed && lock_and_blank(do_lock)) { result = 1; goto cleanup_assertion; }
    int events = events_start(true);
    pid_t child = 0;
    if (command) {
        int error = spawn_command(argv + optind, &child);
        if (error) { diagnostic("%s: %s", argv[optind], strerror(error)); result = error == ENOENT ? 127 : 126; goto cleanup_events; }
    }
    if (isatty(STDERR_FILENO)) diagnostic("ready; closed-lid hold active");
    double started = now_seconds(), last_heartbeat = started, last_lid_check = started;
    while (!stop_requested) {
        if (child) {
            int status; pid_t done = waitpid(child, &status, WNOHANG);
            if (done == child) { result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status); child = 0; break; }
        }
        if (target && !process_alive((pid_t)target, target_birth)) break;
        double now = now_seconds();
        if (timeout && now - started >= (double)timeout) break;
        if (now - last_heartbeat >= HEARTBEAT_SECONDS) {
            if (request(fd, "H\n", reply, sizeof(reply)) || strcmp(reply, "OK")) { diagnostic("hold lost: service unavailable or lease expired"); result = 1; break; }
            last_heartbeat = now_seconds();
        }
        struct pollfd fds[] = {{fd, 0, 0}, {terminal, 0, 0}, {events, POLLIN, 0}};
        /* No read interest on the tty: never consume or spin on the command's input. */
        int wait_ms = 1000;
        if (poll(fds, 3, wait_ms) < 0 && errno != EINTR) { result = 1; break; }
        if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) { diagnostic("service disconnected; hold ended"); result = 1; break; }
        if (terminal >= 0) {
            pid_t foreground;
            if ((fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) || ioctl(terminal, TIOCGPGRP, &foreground)) { stop_requested = SIGHUP; break; }
        }
        bool changed = fds[2].revents & POLLIN;
        if (changed) { char data[64]; while (read(events, data, sizeof(data)) > 0) {} }
        /* Periodic state read also covers a dropped notification, and startup notification races. */
        int state = closed;
        if (changed || now_seconds() - last_lid_check >= 5) { state = lid_closed(); last_lid_check = now_seconds(); }
        if (state < 0) { diagnostic("lost lid-state access"); result = 1; break; }
        if (state != closed) {
            closed = state; stop_assertions(&assertion);
            double remaining = (double)timeout - (now_seconds() - started);
            if (timeout && remaining <= 0) break;
            unsigned long long seconds_left = timeout ? (unsigned long long)remaining + 1 : 0;
            assertion = start_assertions(flags, closed, seconds_left);
            if (assertion < 0 || (closed && lock_and_blank(do_lock))) { result = 1; break; }
        }
        if (assertion > 0 && waitpid(assertion, NULL, WNOHANG) == assertion) {
            assertion = 0;
            if (!timeout || now_seconds() - started < (double)timeout) { diagnostic("caffeinate exited unexpectedly"); result = 1; }
            break;
        }
    }
    if (stop_requested) { result = 128 + stop_requested; if (child) kill(child, stop_requested); }
    /* Losing the hold never force-kills a user's workload. Its terminal/shell owns job cleanup. */
cleanup_events:
    if (events >= 0) close(events);
cleanup_assertion:
    stop_assertions(&assertion);
release:
    if (request(fd, "R\n", reply, sizeof(reply)) || strcmp(reply, "OK")) {
        diagnostic("release not acknowledged; service recovery will retry"); if (!result) result = 1;
    }
    close(fd); if (terminal >= 0) close(terminal); return result;
}
