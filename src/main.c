#include "dshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int lid_closed(void), lock_available(void);
extern bool setting_alias(void);
int status_main(bool doctor) {
    int fd = connect_service(); char reply[512] = ""; int ok = 0;
    if (fd >= 0) {
        ok = request(fd, "S\n", reply, sizeof(reply)) == 0 && !strncmp(reply, "OK ", 3);
        close(fd);
    }
    printf("Doubleshot %s\n", VERSION);
    printf("Service: %s\n", ok ? reply + 3 : "unavailable (run dshot install)");
    if (doctor) {
        printf("SleepDisabled: %d\n", power_read());
        printf("Recovery pending: %s\n", access(paths.journal, F_OK) == 0 ? "yes" : "no");
        printf("Lid: %s\n", lid_closed() == 1 ? "closed" : lid_closed() == 0 ? "open" : "unavailable");
        printf("Lock API: %s\n", lock_available() ? "available (physical verification still required)" : "unavailable");
        char buf[4096]; char *args[] = {"/bin/launchctl", "print", "system/" REAPER_LABEL, NULL};
        printf("Recovery job: %s\n", run_bounded(args[0], args, buf, sizeof(buf), 3) == 0 ? "loaded" : "not loaded");
    }
    return ok ? 0 : 1;
}
int main(int argc, char **argv) {
    init_paths();
    if (argc >= 2) {
        if (!strcmp(argv[1], "_caffeinate")) {
            if (setting_alias()) return client_main(argc - 1, argv + 1);
            argv[1] = "/usr/bin/caffeinate";
            execv(argv[1], argv + 1); diagnostic("cannot execute /usr/bin/caffeinate"); return 1;
        }
        if (!strcmp(argv[1], "--version")) { puts("dshot " VERSION); return 0; }
        if (!strcmp(argv[1], "--help")) { char *help[] = {argv[0], "-h", NULL}; return client_main(2, help); }
        if (!strcmp(argv[1], "_daemon") && argc == 2) return daemon_main();
        if (!strcmp(argv[1], "_reap") && argc == 2) return reaper_main();
        if ((!strcmp(argv[1], "status") || !strcmp(argv[1], "doctor")) && argc == 2) return status_main(!strcmp(argv[1], "doctor"));
        if (!strcmp(argv[1], "config") || !strcmp(argv[1], "shell-init")) return settings_main(argc, argv);
        if (!strcmp(argv[1], "install") || !strcmp(argv[1], "uninstall")) {
            uid_t owner = getuid(); const char *sudo_uid = getenv("SUDO_UID");
            if (owner == 0 && sudo_uid) owner = (uid_t)strtoul(sudo_uid, NULL, 10);
            if (argc == 4 && !strcmp(argv[2], "--uid")) {
                char *end; unsigned long n = strtoul(argv[3], &end, 10);
                if (*end || n == 0 || n > UINT32_MAX) return 2; owner = (uid_t)n;
            } else if (argc != 2) return 2;
            return install_main(!strcmp(argv[1], "uninstall"), owner);
        }
    }
    return client_main(argc, argv);
}
