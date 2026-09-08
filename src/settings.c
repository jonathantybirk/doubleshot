#include "dshot.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char template[] =
    "# Doubleshot settings. No daemon restart needed.\n"
    "# Apply the caffeinate shortcut with: eval \"$(dshot shell-init zsh)\"\n"
    "# This affects your shell only; /usr/bin/caffeinate stays unchanged.\n"
    "alias_caffeinate = false\n\n"
    "# Lock the session and request display sleep when the lid closes.\n"
    "lock_on_close = true\n";
typedef struct { bool alias, lock; } Settings;
static void config_paths(char *dir, char *file) {
    const char *base = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (base && *base == '/') snprintf(dir, PATH_MAX, "%s/doubleshot", base);
    else if (home && *home == '/') snprintf(dir, PATH_MAX, "%s/.config/doubleshot", home);
    else { diagnostic("HOME or XDG_CONFIG_HOME must be an absolute path"); exit(1); }
    snprintf(file, PATH_MAX, "%s/config.toml", dir);
}
static Settings read_settings(void) {
    Settings s = {.alias=false, .lock=true}; char dir[PATH_MAX], file[PATH_MAX]; config_paths(dir, file);
    FILE *f = fopen(file, "r");
    if (!f) { if (errno != ENOENT) { diagnostic("cannot read %s", file); exit(1); } return s; }
    char line[512]; unsigned number = 0;
    while (fgets(line, sizeof(line), f)) {
        number++; char *comment = strchr(line, '#'); if (comment) *comment = 0;
        char *p = line; while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) continue;
        char key[64], value[16], extra; bool boolean;
        if (sscanf(p, " %63[a-z_] = %15s %c", key, value, &extra) != 2 ||
            (strcmp(value, "true") && strcmp(value, "false"))) {
            diagnostic("invalid boolean setting at %s:%u", file, number); fclose(f); exit(2);
        }
        boolean = !strcmp(value, "true");
        if (!strcmp(key, "alias_caffeinate")) s.alias = boolean;
        else if (!strcmp(key, "lock_on_close")) s.lock = boolean;
        else { diagnostic("unknown setting '%s' at %s:%u", key, file, number); fclose(f); exit(2); }
    }
    if (ferror(f)) { fclose(f); diagnostic("cannot read settings"); exit(1); }
    fclose(f); return s;
}
bool setting_lock(void) { return read_settings().lock; }
bool setting_alias(void) { return read_settings().alias; }
int settings_main(int argc, char **argv) {
    char dir[PATH_MAX], file[PATH_MAX]; config_paths(dir, file);
    if (!strcmp(argv[1], "shell-init")) {
        if (argc > 3) return 2;
        const char *shell = argc == 3 ? argv[2] : "zsh";
        if (strcmp(shell, "zsh") && strcmp(shell, "bash") && strcmp(shell, "fish")) { diagnostic("supported shells: zsh, bash, fish"); return 2; }
        /* Dispatch checks the file on each invocation, so edits take effect in existing shells. */
        if (!strcmp(shell, "fish")) puts("function caffeinate; command dshot _caffeinate $argv; end");
        else puts("function caffeinate { command dshot _caffeinate \"$@\"; }");
        return 0;
    }
    if (argc == 3 && !strcmp(argv[2], "init")) {
        char parent[PATH_MAX]; strlcpy(parent, dir, sizeof(parent)); char *slash = strrchr(parent, '/'); if (slash) *slash = 0;
        if ((mkdir(parent, 0755) && errno != EEXIST) || (mkdir(dir, 0755) && errno != EEXIST)) { diagnostic("cannot create %s", dir); return 1; }
        /* Never overwrite user settings. */
        FILE *f = fopen(file, "wx");
        if (!f) { if (errno == EEXIST) { puts(file); return 0; } diagnostic("cannot create %s", file); return 1; }
        int rc = fputs(template, f) < 0; if (fclose(f)) rc = 1;
        puts(file); return rc;
    }
    if (argc != 2) return 2;
    Settings s = read_settings();
    printf("# %s\nalias_caffeinate = %s\nlock_on_close = %s\n", file, s.alias ? "true" : "false", s.lock ? "true" : "false");
    return 0;
}
