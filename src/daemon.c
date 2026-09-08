#include "dshot.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

extern int events_start(bool lid);
static volatile sig_atomic_t stopping;
static void stop_signal(int number) { (void)number; stopping = 1; }
typedef struct { int fd; bool leased; double expires; pid_t pid; uint64_t birth;
    char input[32]; size_t used; } Client;
static Client clients[MAX_CLIENTS];
static bool owned;

static int writer_lock(void) {
    /* Intentionally inherited by pmset: an orphaned mutation still holds the writer lock. */
    int fd = open(paths.lock, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) || st.st_uid != geteuid() || !S_ISREG(st.st_mode) || (st.st_mode & 0022) || flock(fd, LOCK_EX | LOCK_NB)) {
        close(fd); return -1;
    }
    return fd;
}
static int restore(void) {
    if (power_set(0)) { diagnostic("restoration failed; recovery record retained"); return -1; }
    if (unlink(paths.journal) && errno != ENOENT) return -1;
    owned = false;
    diagnostic("normal sleep restored"); return 0;
}
static int heartbeat(void) {
    char text[64]; snprintf(text, sizeof(text), "%.6f\n", now_seconds());
    return atomic_text(paths.heartbeat, text, false);
}
static int acquire(void) {
    if (owned) return 0;
    int baseline = power_read();
    if (baseline != 0) { diagnostic("cannot acquire: SleepDisabled is %d; another tool may own it", baseline); return -1; }
    char record[128];
    snprintf(record, sizeof(record), "%d %llu %.6f\n", getpid(), (unsigned long long)process_birth(getpid()), now_seconds());
    /* Journal and recovery heartbeat must exist before the global mutation. */
    if (atomic_text(paths.journal, record, true)) return -1;
    owned = true;
    if (heartbeat() || power_set(1)) { restore(); return -1; }
    diagnostic("closed-lid hold enabled"); return 0;
}
static void drop(Client *c) { if (c->fd >= 0) close(c->fd); memset(c, 0, sizeof(*c)); c->fd = -1; }
static int count_leases(void) {
    int n = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) n += clients[i].fd >= 0 && clients[i].leased;
    return n;
}
static void answer(Client *c, const char *text) {
    /* Replies are bounded and never block the root event loop. A slow peer loses its lease. */
    size_t len = strlen(text);
    if (send(c->fd, text, len, MSG_DONTWAIT) != (ssize_t)len) drop(c);
}
static void handle(Client *c, const char *line) {
    if (!strcmp(line, "A")) {
        if (!c->leased && acquire()) { answer(c, "ERR cannot enable sleep override; check dshot doctor\n"); return; }
        c->leased = true; c->expires = now_seconds() + LEASE_SECONDS;
        answer(c, "OK\n");
    } else if (!strcmp(line, "H")) {
        if (!c->leased) { answer(c, "ERR lease expired\n"); return; }
        c->expires = now_seconds() + LEASE_SECONDS; answer(c, "OK\n");
    } else if (!strcmp(line, "R")) {
        c->leased = false;
        if (count_leases() == 0 && owned && restore()) { answer(c, "ERR restoration pending\n"); return; }
        answer(c, "OK\n");
    } else if (!strcmp(line, "S")) {
        char reply[256]; snprintf(reply, sizeof(reply), "OK version=%s sessions=%d owned=%d SleepDisabled=%d\n", VERSION, count_leases(), owned, power_read());
        answer(c, reply);
    } else { answer(c, "ERR unknown request\n"); }
}
static void read_client(Client *c) {
    char buf[32]; ssize_t n = recv(c->fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n <= 0) { if (n == 0 || (errno != EAGAIN && errno != EINTR)) drop(c); return; }
    for (ssize_t j = 0; j < n && c->fd >= 0; j++) {
        if (buf[j] == '\n') {
            c->input[c->used] = 0; handle(c, c->input); c->used = 0;
        } else if (c->used + 1 < sizeof(c->input) && buf[j] >= ' ' && buf[j] <= '~') c->input[c->used++] = buf[j];
        else drop(c);
    }
}
static int privileged_context(void) {
#ifndef DSHOT_TEST
    if (geteuid() != 0) { diagnostic("this internal mode requires root"); return -1; }
#endif
    if (secure_directory(paths.state) || secure_directory(paths.run)) { diagnostic("unsafe service directory"); return -1; }
    return 0;
}
int daemon_main(void) {
    if (privileged_context()) return 1;
    /* Every power subprocess inherits this dedicated process group. The reaper can fence all
       old writers before taking over, including a pmset stopped midway through an enable. */
    if (getpgrp() != getpid() && setpgid(0, 0)) { diagnostic("cannot isolate writer process group"); return 1; }
    int lock = writer_lock(); if (lock < 0) { diagnostic("another writer is active"); return 1; }
    if (access(paths.journal, F_OK) == 0 && restore()) { close(lock); return 1; }
    char owner_text[32];
    if (read_text(paths.owner, owner_text, sizeof(owner_text))) { diagnostic("missing owner registration"); close(lock); return 1; }
    char *end; unsigned long parsed = strtoul(owner_text, &end, 10);
    if (end == owner_text || (*end != '\n' && *end) || parsed == 0 || parsed > UINT32_MAX) { close(lock); return 1; }
    uid_t owner = (uid_t)parsed;
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) { close(lock); return 1; }
    fcntl(server, F_SETFD, FD_CLOEXEC); fcntl(server, F_SETFL, O_NONBLOCK);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    if (strlen(paths.socket) >= sizeof(addr.sun_path)) { close(server); close(lock); return 1; }
    strlcpy(addr.sun_path, paths.socket, sizeof(addr.sun_path)); unlink(paths.socket);
    mode_t previous = umask(0077);
    int rc = bind(server, (struct sockaddr *)&addr, sizeof(addr)); umask(previous);
    if (rc || chown(paths.socket, owner, (gid_t)-1) || chmod(paths.socket, 0600) || listen(server, 16)) {
        diagnostic("socket setup: %s", strerror(errno)); close(server); close(lock); return 1;
    }
    signal(SIGPIPE, SIG_IGN); signal(SIGTERM, stop_signal); signal(SIGINT, stop_signal);
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;
    int events = events_start(false);
    double last_hb = 0, last_verify = 0;
    diagnostic("service ready (version %s)", VERSION);
    while (!stopping) {
        double now = now_seconds();
        for (int i = 0; i < MAX_CLIENTS; i++) if (clients[i].fd >= 0 &&
            (now >= clients[i].expires || !process_alive(clients[i].pid, clients[i].birth))) drop(&clients[i]);
        if (owned && count_leases() == 0) {
            if (restore()) { /* Give the reaper/launchd a fresh process to retry the reset. */ rc = 1; break; }
        }
        if (owned && now - last_hb >= 2) {
            if (heartbeat()) { rc = 1; break; } last_hb = now;
        }
        struct pollfd fds[MAX_CLIENTS + 2];
        fds[0] = (struct pollfd){server, POLLIN, 0}; fds[1] = (struct pollfd){events, POLLIN, 0};
        for (int i = 0; i < MAX_CLIENTS; i++) fds[i + 2] = (struct pollfd){clients[i].fd, POLLIN, 0};
        if (poll(fds, MAX_CLIENTS + 2, 1000) < 0) { if (errno == EINTR) continue; rc = 1; break; }
        bool power_event = fds[1].revents & POLLIN;
        if (power_event) { char data[64]; while (read(events, data, sizeof(data)) > 0) {} }
        /* Reconcile power-source transitions immediately; periodically verify as a backstop. */
        now = now_seconds();
        if (owned && count_leases() && (power_event || now - last_verify >= 10)) {
            if ((power_event || power_read() != 1) && power_set(1)) { rc = 1; break; }
            last_verify = now_seconds();
        }
        /* Existing peers first: process releases before admitting more work. */
        for (int i = 0; i < MAX_CLIENTS; i++) if (fds[i + 2].revents) {
            if (now_seconds() >= clients[i].expires) drop(&clients[i]);
            else if (fds[i + 2].revents & POLLIN) read_client(&clients[i]);
            else drop(&clients[i]);
        }
        if (fds[0].revents & POLLIN) {
            int fd = accept(server, NULL, NULL);
            if (fd >= 0) {
                fcntl(fd, F_SETFD, FD_CLOEXEC); fcntl(fd, F_SETFL, O_NONBLOCK);
                uid_t uid; gid_t gid; pid_t pid = 0; socklen_t len = sizeof(pid);
                if (getpeereid(fd, &uid, &gid) || uid != owner || getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &pid, &len)) close(fd);
                else {
                    int slot = 0; while (slot < MAX_CLIENTS && clients[slot].fd >= 0) slot++;
                    if (slot == MAX_CLIENTS) close(fd);
                    else clients[slot] = (Client){.fd=fd, .pid=pid, .birth=process_birth(pid), .expires=now_seconds()+8};
                }
            }
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) drop(&clients[i]);
    if (owned && restore()) rc = 1;
    unlink(paths.socket); close(server); if (events >= 0) close(events); close(lock);
    return rc != 0;
}

int reaper_main(void) {
    if (privileged_context()) return 1;
    if (access(paths.journal, F_OK)) return 0;
    int lock = writer_lock();
    if (lock < 0) {
        char hb[64], record[128]; double timestamp = 0, created = 0;
        int pid = 0; unsigned long long birth = 0;
        if (read_text(paths.journal, record, sizeof(record)) || sscanf(record, "%d %llu %lf", &pid, &birth, &created) != 3 ||
            pid <= 1 || birth == 0 || !isfinite(created) || created < 0) return 1;
        if (read_text(paths.heartbeat, hb, sizeof(hb)) == 0) timestamp = strtod(hb, NULL);
        if (timestamp < created) timestamp = created;
        double age = now_seconds() - timestamp;
        if (age >= 0 && age < STALE_SECONDS) return 0;
        /* Never act on a PID alone. This record is root-written and the start time must match. */
        uint64_t current_birth = process_birth(pid);
        if (current_birth && (current_birth != (uint64_t)birth || getpgid(pid) != pid)) return 1;
        diagnostic("writer heartbeat stalled; fencing its process group");
        if (kill(-pid, SIGKILL) && errno != ESRCH) return 1;
        double end = now_seconds() + 3;
        do { usleep(20000); lock = writer_lock(); } while (lock < 0 && now_seconds() < end);
        if (lock < 0) return 1;
    }
    int result = 0;
    if (access(paths.journal, F_OK) == 0) result = restore();
    close(lock); return result ? 1 : 0;
}
