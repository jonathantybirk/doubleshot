#ifndef DSHOT_H
#define DSHOT_H
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <limits.h>

#define VERSION "1.0"
#define LABEL "io.doubleshot.daemon"
#define REAPER_LABEL "io.doubleshot.reaper"
#define INSTALL_DIR "/Library/PrivilegedHelperTools/io.doubleshot"
#define INSTALLED INSTALL_DIR "/dshot"
#define MAX_CLIENTS 64
#define LEASE_SECONDS 20.0
#define HEARTBEAT_SECONDS 5.0
#define STALE_SECONDS 15.0

typedef struct { char run[PATH_MAX], state[PATH_MAX], socket[PATH_MAX], lock[PATH_MAX],
    journal[PATH_MAX], heartbeat[PATH_MAX], owner[PATH_MAX]; } Paths;
extern Paths paths;
void init_paths(void);
double now_seconds(void);
uint64_t process_birth(pid_t pid);
bool process_alive(pid_t pid, uint64_t birth);
int atomic_text(const char *path, const char *text, bool durable);
int read_text(const char *path, char *buffer, size_t size);
int secure_directory(const char *path);
int run_bounded(const char *file, char *const argv[], char *out, size_t size, double seconds);
int power_read(void);
int power_set(int value);
int connect_service(void);
int request(int fd, const char *line, char *reply, size_t size);
int daemon_main(void);
int reaper_main(void);
int install_main(bool uninstall, uid_t uid);
int client_main(int argc, char **argv);
int status_main(bool doctor);
void diagnostic(const char *format, ...) __attribute__((format(printf,1,2)));
#endif
