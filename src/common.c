#include "dshot.h"
#include <errno.h>
#include <fcntl.h>
#include <libproc.h>
#include <mach/mach_time.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

Paths paths;
void diagnostic(const char *format, ...) {
    va_list ap; va_start(ap, format); fprintf(stderr, "dshot: ");
    vfprintf(stderr, format, ap); fprintf(stderr, "\n"); va_end(ap);
}
void init_paths(void) {
    const char *run = "/var/run/doubleshot", *state = "/var/db/doubleshot";
#ifdef DSHOT_TEST
    const char *root = getenv("DSHOT_TEST_ROOT");
    if (!root || root[0] != '/' || geteuid() == 0) {
        diagnostic("test binary requires a non-root user and DSHOT_TEST_ROOT"); exit(1);
    }
    run = root; state = root;
#endif
    snprintf(paths.run, sizeof(paths.run), "%s", run);
    snprintf(paths.state, sizeof(paths.state), "%s", state);
    snprintf(paths.socket, sizeof(paths.socket), "%s/control.sock", run);
    snprintf(paths.heartbeat, sizeof(paths.heartbeat), "%s/heartbeat", run);
    snprintf(paths.lock, sizeof(paths.lock), "%s/writer.lock", state);
    snprintf(paths.journal, sizeof(paths.journal), "%s/restore", state);
    snprintf(paths.owner, sizeof(paths.owner), "%s/owner", state);
}
double now_seconds(void) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (double)mach_continuous_time() * tb.numer / tb.denom / 1e9;
}
uint64_t process_birth(pid_t pid) {
    struct proc_bsdinfo info;
    if (pid <= 1 || proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info)) return 0;
    if (info.pbi_status == 5 /* SZOMB */) return 0;
    return (uint64_t)info.pbi_start_tvsec * 1000000 + info.pbi_start_tvusec;
}
bool process_alive(pid_t pid, uint64_t birth) { return birth && process_birth(pid) == birth; }
int secure_directory(const char *path) {
    if (mkdir(path, 0755) && errno != EEXIST) return -1;
    struct stat st;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) {
        errno = EPERM; return -1;
    }
    return 0;
}
int read_text(const char *path, char *buffer, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size >= (off_t)size) { close(fd); errno = EINVAL; return -1; }
    ssize_t n = read(fd, buffer, size - 1); close(fd);
    if (n < 0) return -1;
    buffer[n] = 0; return 0;
}
int atomic_text(const char *path, const char *text, bool durable) {
    char tmp[PATH_MAX]; snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    int fd = mkstemp(tmp); if (fd < 0) return -1;
    fchmod(fd, 0644);
    size_t len = strlen(text), done = 0;
    int error = 0;
    while (done < len) {
        ssize_t n = write(fd, text + done, len - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { error = 1; break; } done += (size_t)n;
    }
    if (!error && durable && fsync(fd)) error = 1;
    if (close(fd)) error = 1;
    if (!error && rename(tmp, path)) error = 1;
    if (error) { unlink(tmp); return -1; }
    if (durable) {
        char parent[PATH_MAX]; snprintf(parent, sizeof(parent), "%s", path);
        char *slash = strrchr(parent, '/'); if (slash) *slash = 0;
        int dir = open(parent, O_RDONLY | O_CLOEXEC);
        if (dir < 0) return -1;
        int rc = fsync(dir); close(dir); if (rc) return -1;
    }
    return 0;
}
/* Fixed executable/argv at every privileged call site. No shell and no inherited environment. */
int run_bounded(const char *file, char *const argv[], char *out, size_t size, double seconds) {
    int fds[2]; if (pipe(fds)) return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC); fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    char *env[] = {"PATH=/usr/bin:/bin:/usr/sbin:/sbin", "LANG=C", NULL};
    pid_t child; int error = posix_spawn(&child, file, &actions, NULL, argv, env);
    posix_spawn_file_actions_destroy(&actions); close(fds[1]);
    if (error) { close(fds[0]); errno = error; return -1; }
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    double deadline = now_seconds() + seconds; size_t used = 0; int status = 0;
    if (size) out[0] = 0;
    for (;;) {
        char buf[512]; ssize_t n;
        while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
            size_t take = size > used + 1 ? size - used - 1 : 0;
            if (take > (size_t)n) take = (size_t)n;
            if (take) { memcpy(out + used, buf, take); used += take; out[used] = 0; }
        }
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) break;
        if (result < 0 && errno != EINTR) { status = -1; break; }
        if (now_seconds() >= deadline) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            close(fds[0]); errno = ETIMEDOUT; return -1;
        }
        struct pollfd p = {fds[0], POLLIN, 0}; poll(&p, 1, 20);
    }
    /* Child may have exited between the read and waitpid. Drain the final bytes. */
    if (size > used + 1) {
        ssize_t n = read(fds[0], out + used, size - used - 1);
        if (n > 0) out[used + (size_t)n] = 0;
    }
    close(fds[0]);
    return status >= 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
int power_read(void) {
#ifdef DSHOT_TEST
    char file[PATH_MAX], buf[16]; snprintf(file, sizeof(file), "%s/power", paths.state);
    if (read_text(file, buf, sizeof(buf))) return 0;
    return atoi(buf) == 1;
#else
    char buf[4096]; char *args[] = {"/usr/bin/pmset", "-g", NULL};
    if (run_bounded(args[0], args, buf, sizeof(buf), 3)) return -1;
    char *p = strstr(buf, "SleepDisabled");
    if (!p) return 0; /* macOS omits this key before its first use. */
    p += strlen("SleepDisabled"); while (*p == ' ' || *p == '\t') p++;
    return *p == '0' ? 0 : *p == '1' ? 1 : -1;
#endif
}
int power_set(int value) {
#ifdef DSHOT_TEST
    char file[PATH_MAX]; snprintf(file, sizeof(file), "%s/fail-%d", paths.state, value);
    if (access(file, F_OK) == 0) { errno = EIO; return -1; }
    snprintf(file, sizeof(file), "%s/power", paths.state);
    return atomic_text(file, value ? "1\n" : "0\n", true);
#else
    char *args[] = {"/usr/bin/pmset", "-a", "disablesleep", value ? "1" : "0", NULL};
    if (run_bounded(args[0], args, NULL, 0, 3)) return -1;
    return power_read() == value ? 0 : -1;
#endif
}
int connect_service(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0); if (fd < 0) return -1;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    if (strlen(paths.socket) >= sizeof(addr.sun_path)) { close(fd); errno = ENAMETOOLONG; return -1; }
    strlcpy(addr.sun_path, paths.socket, sizeof(addr.sun_path));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) { close(fd); return -1; }
    uid_t uid; gid_t gid;
    if (getpeereid(fd, &uid, &gid) || uid !=
#ifdef DSHOT_TEST
        getuid()
#else
        0
#endif
    ) { close(fd); errno = EPERM; return -1; }
    struct timeval timeout = {8, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return fd;
}
int request(int fd, const char *line, char *reply, size_t size) {
    size_t len = strlen(line), done = 0;
    while (done < len) { ssize_t n = write(fd, line + done, len - done); if (n < 0 && errno == EINTR) continue; if (n <= 0) return -1; done += (size_t)n; }
    size_t used = 0;
    while (used + 1 < size) {
        ssize_t n = read(fd, reply + used, 1); if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        if (reply[used++] == '\n') { reply[used - 1] = 0; return 0; }
    }
    errno = EMSGSIZE; return -1;
}
