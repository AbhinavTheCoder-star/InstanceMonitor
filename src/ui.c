#include "instmon.h"
#include "sysmon.h"
#include <curses.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static volatile sig_atomic_t stopping;
static void stop_requested(int signal_number) { (void)signal_number; stopping = 1; }

/* Clip every line to the terminal width. A negative or zero width is avoided
 * so tiny terminals cannot accidentally request an unbounded curses write. */
static void line(int row, const char *text)
{
    if (row >= 0 && row < LINES && COLS > 1) mvaddnstr(row, 0, text, COLS - 1);
}

static void document(const char *text, size_t skip)
{
    const char *start = text;
    for (size_t i = 0; i < skip && *start; ++i) {
        const char *next = strchr(start, '\n');
        if (!next) return;
        start = next + 1;
    }
    for (int row = 4; row < LINES - 2 && *start; ++row) {
        const char *end = strchr(start, '\n');
        size_t length = end ? (size_t)(end - start) : strlen(start);
        char buffer[TEXT_CAP];
        if (length >= sizeof buffer) length = sizeof buffer - 1;
        memcpy(buffer, start, length); buffer[length] = 0;
        line(row, buffer);
        if (!end) break;
        start = end + 1;
    }
}

int ui_run(const Options *initial)
{
    Options options = *initial;
    Sysmon system = {.pipe_fd = -1};
    bool sysmon = options.sysmon, system_ready = false;
    size_t system_scroll = 0;
    Snapshot current = {0}, previous = {0};
    Process selected = {0}, pending = {0};
    bool confirming = false, selection_cleared = false;
    char notice[512] = "";
    Details details = {0};
    size_t index = 0, scroll = 0;
    bool inspecting = false, searching = false, help = false;
    char filter[256];
    snprintf(filter, sizeof filter, "%s", options.filter); options.filter = filter;
    int panel = 0, result = 0;
    double deadline = 0, details_at = 0;
    const char *panels[] = {"Overview", "Status / threads", "Memory", "Files / socket inodes", "Limits"};
    struct sigaction action = {0}, old_int, old_term;
    action.sa_handler = stop_requested; sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, &old_int); sigaction(SIGTERM, &action, &old_term);
    if (!initscr()) return 1;
    cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0); timeout(50);
    /* Escape is a cancellation key: do not wait ncurses' default second for
     * an optional terminal-key sequence before honoring it. */
    set_escdelay(25);
    while (!stopping) {
        double now = monotonic_seconds();
        if (now >= deadline) {
            Snapshot next = {0};
            if (snapshot_collect(&next)) { result = 1; break; }
            snapshot_rates(&next, &current);
            snapshot_free(&previous); previous = current; current = next;
            /* Keep current unfiltered for the next rate calculation. A separate
             * view below borrows copied records, so changing search loses no history. */
            if (deadline == 0) deadline = now;
            do { deadline += options.interval; } while (deadline <= monotonic_seconds());
        }
        if (sysmon) {
            if (!system_ready) { sysmon_init(&system, ""); system_ready = true; }
            sysmon_refresh(&system, now);
        }
        if (system_ready) sysmon_poll(&system, now);
        Snapshot view = current;
        /* Allocate only a view array; all strings are inline and safe to copy. */
        view.items = NULL;
        if (!sysmon && current.count) {
            view.items = malloc(current.count * sizeof *view.items);
            if (!view.items) { result = 1; break; }
            memcpy(view.items, current.items, current.count * sizeof *view.items);
        }
        if (sysmon) view.count = 0;
        else snapshot_query(&view, &options);
        if (view.count && index >= view.count) index = view.count - 1;
        bool selected_visible = false;
        for (size_t i = 0; i < view.count; ++i) {
            if (view.items[i].pid == selected.pid && view.items[i].start_ticks == selected.start_ticks) {
                index = i; selected_visible = true;
                if (!confirming && !inspecting) selected = view.items[i];
                break;
            }
        }
        /* Never replace an exited or filtered-out selection with a neighboring
         * row. Escape deliberately leaves no selection until the user navigates. */
        if (!selected.pid && !selection_cleared && view.count && !confirming) {
            selected = view.items[index]; selected_visible = true;
        }
        const Process *live = snapshot_find(&current, selected.pid, selected.start_ticks);
        erase();
        char buffer[TEXT_CAP * 4];
        snprintf(buffer, sizeof buffer, "InstanceMonitor | %zu processes | RAM %.0f / %.0f MiB available | %ld CPUs | refresh %.3fs",
                 sysmon ? current.count : view.count, (double)current.memory_available / 1048576, (double)current.memory_total / 1048576, current.cpus, current.duration);
        line(0, buffer);
        if (help) {
            document("Arrows: select processes or scroll; j: down\nk: request SIGTERM on home screen; scroll up in diagnostics\nEnter: inspect process\nTab: next diagnostic panel\nEscape: cancel prompt, search or selection; return from diagnostics\n/: search by name, command or PID; Enter finishes; Escape clears\ns: open Sysmon; m: return to main monitor\nShift+S: cycle RAM, CPU, name, PID sort\nr: reverse sort\nq: quit (outside search)\n\nRAM means RSS; shared pages can count in multiple processes.\nCPU: 100% is one logical CPU; first sample warms up.\nDetails refresh every five seconds. Permission restrictions are shown.\nTermination requires k then Enter; Escape cancels. Only SIGTERM is sent.", 0);
        } else if (sysmon) {
            snprintf(buffer, sizeof buffer, "System Monitor (Sysmon) | diagnostic age %.1fs | j/k: scroll", now - system.refreshed_at);
            line(2, buffer);
            sysmon_format(&system, &current, buffer, sizeof buffer);
            document(buffer, system_scroll);
        } else if (inspecting) {
            if (live) {
                selected = *live;
                if (now - details_at >= 5 || details_at == 0) { details_collect(&selected, &details); details_at = now; }
            }
            snprintf(buffer, sizeof buffer, "PID %ld: %s | %s | %s | detail age %.1fs", (long)selected.pid, selected.name,
                     panels[panel], live && details.identity_valid ? "live" : "exited / identity changed; last observed data", now - details_at);
            line(2, buffer);
            if (panel == 0) {
                char cpu[32] = "warming up";
                if (selected.rates_valid) snprintf(cpu, sizeof cpu, "%.1f%%", selected.cpu_percent);
                snprintf(buffer, sizeof buffer,
                         "Command: %s\nExecutable: %s\nDirectory: %s\nState: %c | CPU: %s | Threads: %" PRIu64 "\nRAM: %.2f MiB | Virtual: %.2f MiB\nOwner: %s | Parent: %ld | Age: %.0fs\nI/O: %s\n",
                         selected.command, details.identity_valid ? details.executable : "unavailable",
                         details.identity_valid ? details.directory : "unavailable", selected.state, cpu, selected.threads, (double)selected.rss / 1048576,
                         (double)selected.virtual_bytes / 1048576, selected.owner, (long)selected.parent, selected.age, value_status(selected.io_status));
                document(buffer, scroll);
            } else if (details.identity_valid) {
                const char *documents[] = {"", details.status, details.memory, details.files, details.limits};
                document(documents[panel], scroll);
            }
        } else {
            snprintf(buffer, sizeof buffer, "Filter: %s%s | sort: %s%s", filter, searching ? "_" : "", (const char *[]){"RAM", "CPU", "name", "PID"}[options.sort], options.reverse ? " reversed" : "");
            line(1, buffer);
            line(3, "    PID OWNER        S    CPU%    RAM MiB   RAM% THREADS NAME");
            int visible = LINES > 7 ? LINES - 7 : 1;
            size_t top = index >= (size_t)visible ? index - (size_t)visible + 1 : 0;
            for (size_t i = top; i < view.count && i < top + (size_t)visible; ++i) {
                Process *p = &view.items[i]; char cpu[32] = "warm";
                if (p->rates_valid) snprintf(cpu, sizeof cpu, "%.1f", p->cpu_percent);
                snprintf(buffer, sizeof buffer, "%7ld %-12.12s %c %7s %10.2f %6.2f %7" PRIu64 " %s",
                         (long)p->pid, p->owner, p->state, cpu, (double)p->rss / 1048576,
                         (double)p->rss / (double)current.memory_total * 100, p->threads, p->name);
                if (selected_visible && i == index) attron(A_REVERSE);
                line(4 + (int)(i - top), buffer);
                if (selected_visible && i == index) attroff(A_REVERSE);
            }
            if (!view.count) line(4, "No matching processes.");
            if (notice[0]) line(2, notice);
            else if (selected.pid && !selected_visible)
                line(2, "Selected process left the list; use arrows to select again.");
        }
        if (confirming && (LINES < 5 || COLS < 40)) {
            confirming = false;
            snprintf(notice, sizeof notice, "Terminal too small; termination cancelled");
        }
        if (confirming) {
            snprintf(buffer, sizeof buffer, "Terminate PID %ld (%s)? Enter: SIGTERM  Esc: cancel", (long)pending.pid, pending.name);
            attron(A_REVERSE); line(2, buffer); attroff(A_REVERSE);
        }
        const char *footer = "Arrows:move k:terminate /:search Esc:cancel Enter:inspect s:Sysmon m:main S:sort r:rev Tab:panel ?:help q:quit";
        if (sysmon || inspecting) footer = "Arrows/j/k:scroll Tab:panel Esc:back s:Sysmon m:main S:sort r:reverse ?:help q:quit";
        if (searching) footer = "Type name or PID  Enter: finish search  Esc: clear search and selection";
        if (confirming) footer = "Enter: confirm SIGTERM  Esc: cancel";
        line(LINES - 1, footer);
        refresh();
        int key = getch();
        if (confirming) {
            /* Freeze the target captured by k. Refreshes and other keys cannot
             * change it, and Enter is consumed here rather than opening details. */
            if (key == 27) { confirming = false; snprintf(notice, sizeof notice, "Termination cancelled; no signal sent"); }
            else if (key == '\n' || key == KEY_ENTER) {
                TerminateResult outcome = process_terminate(&pending);
                snprintf(notice, sizeof notice, "PID %ld (%s): %s", (long)pending.pid, pending.name, termination_message(outcome));
                confirming = false;
            }
        } else if (searching) {
            size_t length = strlen(filter);
            if (key == '\n' || key == KEY_ENTER) searching = false;
            else if (key == 27) { filter[0] = 0; searching = false; }
            else if ((key == KEY_BACKSPACE || key == 127 || key == 8) && length) filter[length - 1] = 0;
            else if (key >= 32 && key < 127 && length < sizeof filter - 1) { filter[length] = (char)key; filter[length + 1] = 0; }
            index = 0; selected.pid = 0; selection_cleared = key == 27; notice[0] = 0;
        } else if (key == 'q') stopping = 1;
        else if (key == '?') help = !help;
        else if (key == 27) {
            if (!sysmon && !inspecting && !help) { selected.pid = 0; selection_cleared = true; notice[0] = 0; }
            sysmon = false; inspecting = false; help = false; scroll = 0;
        }
        else if (key == 's') { sysmon = true; help = false; }
        else if (key == 'm') { sysmon = false; inspecting = false; help = false; scroll = 0; }
        else if (key == '/' && !inspecting && !sysmon) searching = true;
        else if (key == 'k' && !sysmon && !inspecting && !help) {
            if (selected_visible) { pending = selected; confirming = true; notice[0] = 0; }
            else snprintf(notice, sizeof notice, "No current process selected; use arrows first");
        }
        else if (key == 'S') options.sort = (SortKey)((options.sort + 1) % 4);
        else if (key == 'r') options.reverse = !options.reverse;
        else if (key == '\t' && inspecting && !sysmon) { panel = (panel + 1) % 5; scroll = 0; }
        else if ((key == '\n' || key == KEY_ENTER) && selected_visible && !inspecting && !sysmon && !help) { inspecting = true; details_at = 0; scroll = 0; }
        else if (key == KEY_DOWN || key == 'j') {
            if (sysmon) { if (system_scroll < 4096) ++system_scroll; }
            else if (inspecting) { if (scroll < 4096) ++scroll; }
            else if (!help && view.count) {
                if (selected_visible && index + 1 < view.count) ++index;
                selected = view.items[index]; selection_cleared = false; notice[0] = 0;
            }
        } else if (key == KEY_UP || key == 'k') {
            if (sysmon) { if (system_scroll) --system_scroll; }
            else if (inspecting) { if (scroll) --scroll; }
            else if (!help && view.count) {
                if (selected_visible && index) --index;
                selected = view.items[index]; selection_cleared = false; notice[0] = 0;
            }
        }
        free(view.items);
    }
    sysmon_destroy(&system);
    endwin();
    sigaction(SIGINT, &old_int, NULL); sigaction(SIGTERM, &old_term, NULL);
    snapshot_free(&current); snapshot_free(&previous);
    if (result) fprintf(stderr, "Unable to collect process data or allocate memory\n");
    return result;
}
