#include "instmon.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool number(const char *text, unsigned long *value)
{
    char *end;
    if (!text[0] || text[0] == '-') return false;
    errno = 0;
    *value = strtoul(text, &end, 10);
    return !errno && !*end;
}

/* Resolve all arguments before collecting data or touching terminal state.
 * Options borrow argv strings; their lifetime therefore covers main(). */
int options_parse(int argc, char **argv, Options *o)
{
    *o = (Options){.interval = 1.0, .filter = ""};
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help")) {
            puts("InstanceMonitor: Linux process diagnostics and management\n"
                 "Usage: instmon [list | inspect PID] [options]\n"
                 "  --sort ram|cpu|name|pid  --reverse  --filter TEXT\n"
                 "  --user NAME|UID  --json  --interval SECONDS (interactive)\n"
                 "  --sysmon (interactive)  --help  --version\n"
                 "Keys: arrows select, k request SIGTERM, Enter confirm/inspect, Esc cancel,\n"
                 "      / search, s Sysmon, m main, S sort, r reverse, ? help, q quit");
            return 1;
        }
        if (!strcmp(arg, "--version")) { puts("instmon 0.3.0"); return 1; }
        if (!strcmp(arg, "list") && !o->list && !o->inspect) { o->list = true; continue; }
        if (!strcmp(arg, "inspect") && !o->list && !o->inspect && i + 1 < argc) {
            unsigned long pid;
            if (!number(argv[++i], &pid) || !pid || pid > INT_MAX) goto invalid;
            o->inspect = true; o->pid = (pid_t)pid; continue;
        }
        if (!strcmp(arg, "--sysmon")) { o->sysmon = true; continue; }
        if (!strcmp(arg, "--json")) { o->json = true; continue; }
        if (!strcmp(arg, "--reverse")) { o->reverse = true; continue; }
        if (i + 1 >= argc) goto invalid;
        if (!strcmp(arg, "--filter")) { o->filter = argv[++i]; continue; }
        if (!strcmp(arg, "--sort")) {
            const char *key = argv[++i];
            if (!strcmp(key, "ram")) o->sort = SORT_RAM;
            else if (!strcmp(key, "cpu")) o->sort = SORT_CPU;
            else if (!strcmp(key, "name")) o->sort = SORT_NAME;
            else if (!strcmp(key, "pid")) o->sort = SORT_PID;
            else goto invalid;
            continue;
        }
        if (!strcmp(arg, "--interval")) {
            char *end;
            errno = 0;
            o->interval = strtod(argv[++i], &end);
            if (errno || *end || !isfinite(o->interval) || o->interval < .25 || o->interval > 3600) goto invalid;
            o->interval_set = true; continue;
        }
        if (!strcmp(arg, "--user")) {
            const char *user = argv[++i];
            unsigned long uid;
            struct passwd *pw = getpwnam(user);
            if (pw) o->uid = pw->pw_uid;
            else if (number(user, &uid) && uid <= UINT_MAX) o->uid = (uid_t)uid;
            else goto invalid;
            o->user_set = true; continue;
        }
invalid:
        fprintf(stderr, "Invalid argument near '%s'; use --help\n", arg);
        return -1;
    }
    if (o->sysmon && (o->list || o->inspect || o->json)) {
        fprintf(stderr, "--sysmon requires the interactive monitor\n");
        return -1;
    }
    if (o->inspect && (o->filter[0] || o->user_set)) {
        fprintf(stderr, "inspect cannot be combined with process filters\n");
        return -1;
    }
    return 0;
}
