#include "dshot.h"
#include <errno.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int launch(const char *verb, const char *arg) {
    char *args[] = {"/bin/launchctl", (char *)verb, (char *)arg, NULL};
    return run_bounded(args[0], args, NULL, 0, 10);
}
static int bootstrap(const char *file) {
    char *args[] = {"/bin/launchctl", "bootstrap", "system", (char *)file, NULL};
    return run_bounded(args[0], args, NULL, 0, 10);
}
static int copy_binary(const char *source) {
    int input = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW); if (input < 0) return -1;
    struct stat st; if (fstat(input, &st) || !S_ISREG(st.st_mode)) { close(input); return -1; }
    char temp[] = INSTALL_DIR "/dshot.XXXXXX"; int output = mkstemp(temp);
    if (output < 0) { close(input); return -1; }
    int rc = 0; char buf[16384]; ssize_t n;
    while ((n = read(input, buf, sizeof(buf))) > 0) {
        ssize_t done = 0; while (done < n) { ssize_t written = write(output, buf + done, (size_t)(n - done)); if (written <= 0) { rc = -1; break; } done += written; }
        if (rc) break;
    }
    if (n < 0 || fchmod(output, 0755) || fchown(output, 0, 0) || fsync(output)) rc = -1;
    close(input); if (close(output)) rc = -1;
    if (!rc && rename(temp, INSTALLED)) rc = -1;
    if (rc) unlink(temp); return rc;
}
static int reset_with_lock(void) {
    int fd = open(paths.lock, O_RDWR | O_CREAT | O_NOFOLLOW, 0600); if (fd < 0) return -1;
    double end = now_seconds() + 12;
    while (flock(fd, LOCK_EX | LOCK_NB)) {
        if (now_seconds() >= end) { close(fd); diagnostic("old writer has not stopped; leaving recovery installed"); return -1; }
        usleep(50000);
    }
    int result = 0;
    if (access(paths.journal, F_OK) == 0) {
        result = power_set(0);
        if (!result && unlink(paths.journal)) result = -1;
    }
    close(fd); return result;
}
int install_main(bool uninstall, uid_t uid) {
#ifdef DSHOT_TEST
    (void)uninstall; (void)uid; diagnostic("installation disabled in test build"); return 1;
#else
    char executable[PATH_MAX], resolved[PATH_MAX]; uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) || !realpath(executable, resolved)) return 1;
    if (getuid() != 0) {
        char owner[32]; snprintf(owner, sizeof(owner), "%u", getuid());
        const char *script = "on run argv\n"
            "do shell script (quoted form of item 1 of argv & \" \" & item 2 of argv & \" --uid \" & item 3 of argv) with administrator privileges\n"
            "end run";
        char *args[] = {"/usr/bin/osascript", "-e", (char *)script, resolved,
                        uninstall ? "uninstall" : "install", owner, NULL};
        execv(args[0], args); diagnostic("cannot open setup: %s", strerror(errno)); return 1;
    }
    if (!uid) { diagnostic("run install as your ordinary user, or supply --uid"); return 2; }
    if (secure_directory(paths.state) || secure_directory(paths.run) || secure_directory("/Library/PrivilegedHelperTools") || secure_directory(INSTALL_DIR)) {
        diagnostic("installation directory is not root-controlled"); return 1;
    }
    const char *daemon_plist = "/Library/LaunchDaemons/" LABEL ".plist";
    const char *reaper_plist = "/Library/LaunchDaemons/" REAPER_LABEL ".plist";
    /* Stop acquisition first. Keep the old reaper available until verified restoration. */
    launch("bootout", "system/" LABEL);
    if (reset_with_lock()) { diagnostic("cannot verify restoration; installation unchanged"); return 1; }
    launch("bootout", "system/" REAPER_LABEL);
    if (uninstall) {
        if (unlink(daemon_plist) && errno != ENOENT) return 1;
        if (unlink(reaper_plist) && errno != ENOENT) return 1;
        unlink(INSTALLED); rmdir(INSTALL_DIR);
        unlink(paths.socket); unlink(paths.heartbeat); unlink(paths.owner); unlink(paths.lock);
        rmdir(paths.run); rmdir(paths.state);
        puts("Doubleshot: service removed; normal sleep restored."); return 0;
    }
    if (copy_binary(resolved)) { diagnostic("cannot install helper"); return 1; }
    char owner[32]; snprintf(owner, sizeof(owner), "%u\n", uid);
    if (atomic_text(paths.owner, owner, true)) return 1;
    char plist[4096];
    snprintf(plist, sizeof(plist), "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n<key>Label</key><string>%s</string>\n"
        "<key>ProgramArguments</key><array><string>%s</string><string>_daemon</string></array>\n"
        "<key>RunAtLoad</key><true/><key>KeepAlive</key><true/>\n"
        "<key>ThrottleInterval</key><integer>3</integer>\n"
        "<key>ExitTimeOut</key><integer>10</integer>\n"
        "<key>StandardErrorPath</key><string>/var/log/doubleshot.log</string>\n"
        "</dict></plist>\n", LABEL, INSTALLED);
    if (atomic_text(daemon_plist, plist, true)) return 1;
    snprintf(plist, sizeof(plist), "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n<key>Label</key><string>%s</string>\n"
        "<key>ProgramArguments</key><array><string>%s</string><string>_reap</string></array>\n"
        "<key>RunAtLoad</key><true/><key>StartInterval</key><integer>10</integer>\n"
        "<key>StandardErrorPath</key><string>/var/log/doubleshot.log</string>\n"
        "</dict></plist>\n", REAPER_LABEL, INSTALLED);
    if (atomic_text(reaper_plist, plist, true)) return 1;
    if (bootstrap(reaper_plist) || bootstrap(daemon_plist)) { diagnostic("launchd registration failed; rerun dshot install"); return 1; }
    puts("Doubleshot: helper installed.");
    return 0;
#endif
}
