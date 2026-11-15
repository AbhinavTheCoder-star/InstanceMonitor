#include "instmon.h"
#include <locale.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    Options options;
    setlocale(LC_ALL, "");
    int parsed = options_parse(argc, argv, &options);
    if (parsed != 0) return parsed > 0 ? 0 : 2;

    bool interactive = !options.list && !options.inspect && !options.json &&
                       isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if (options.interval_set && !interactive) {
        fprintf(stderr, "--interval requires the interactive monitor\n");
        return 2;
    }
    if (interactive) return ui_run(&options);

    Snapshot snapshot = {0};
    if (snapshot_collect(&snapshot) != 0) {
        perror("collect /proc");
        return 1;
    }
    snapshot_query(&snapshot, &options);
    if (options.inspect && snapshot.count == 0) {
        fprintf(stderr, "PID %ld is absent or inaccessible\n", (long)options.pid);
        snapshot_free(&snapshot);
        return 1;
    }
    output_snapshot(&snapshot, &options);
    snapshot_free(&snapshot);
    return ferror(stdout) ? 1 : 0;
}
