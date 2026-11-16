# InstanceMonitor — application plan and architecture

## Purpose and GitHub description

**GitHub repository description:** A C-based CLI for monitoring running processes, tracking RAM and CPU usage, and exploring detailed application diagnostics in an interactive terminal.

Build a local process monitor named `instmon`. Users can see running instances, understand their observable activity, compare resource use, and select a process for deeper inspection.

This document is the implementation plan. No application code is part of this planning change.

## 1. Scope and assumptions

- An **instance** means one operating-system process, identified by PID and process start time. Multiple launches of the same executable remain separate instances.
- Target Linux first, matching the current development environment. macOS and Windows are later backends, not initial release promises.
- Enumerate every process visible to the current user through the current PID namespace. Restricted details are marked unavailable; the tool cannot guarantee access to every host process.
- Show processes by default. A process tree shows parent/child relationships; it does not claim to identify every logical application or include detached children automatically.
- “What it is doing” means observed process state, CPU activity, I/O changes, threads, command, and accessible resources. Determining application intent, individual functions, or business operations requires application instrumentation or tracing and is outside the first release.
- Monitoring is read-only; v0.3.0 adds explicit, confirmed SIGTERM on the home screen. Debugging attachment, syscall tracing, container management, and remote monitoring remain future extensions.

## 2. User experience

### Overview

Start `instmon` to open an interactive terminal view. Display a system summary above a sortable process table.

System summary: host RAM total and available, uptime, logical CPU count, process count, last refresh time, and refresh duration.

Table columns: PID, name, owner, state, CPU percentage, resident RAM in MiB, RAM percentage, thread count, and elapsed lifetime. Offer the full command in a toggleable column or detail view. Default sort: resident RAM descending, with PID as a stable tie-breaker.

Support search by name or command, filtering by owner or PID, sorting by RAM/CPU/name/PID, and switching between flat and tree views. Search is case-insensitive literal text in the first release.

### Selecting an instance

Arrow keys move the selection (`j` also moves down); `k` requests confirmed SIGTERM on the home screen and scrolls up in diagnostics; Enter opens details; Escape returns; `q` quits; `/` searches names, commands or PIDs; `s` opens Sysmon; `m` returns to the process monitor; `Shift+S` cycles sort columns; `r` reverses sort; `t` toggles the tree; `?` shows help. In details, Tab cycles diagnostic panels. Show these controls in the footer.

Preserve selection by process identity across refreshes, not table row number. If the selected process exits, show “Process exited” and its last observed values with their timestamp; do not silently select a replacement PID.

### Diagnostic panels

| Panel | Planned information |
| --- | --- |
| Overview | PID, parent PID, owner, command, executable, working directory, state, start time, uptime, observable activity summary |
| Memory | Resident and virtual memory, swap, anonymous/file/shared breakdown when available, proportional memory on demand, recent resident-memory history |
| CPU and threads | CPU percentage, cumulative user/system CPU time, thread count, priority/nice value, per-thread state and CPU usage |
| I/O | Cumulative read/write counters, interval rates, faults, context-switch counters and rates when available |
| Files | Accessible descriptors, target paths, descriptor count; bounded and scrollable enumeration |
| Connections | Socket ownership and available TCP/UDP endpoint/state information; UNIX sockets where supported |
| Relationships | Parent and child processes with identity, state, and resource use |
| Limits | Accessible process resource limits and relevant status fields |

Each value carries a status: available, warming up, permission denied, unsupported, exited, or read error. Missing values render as explanatory labels, never as invented zeroes. Show expensive panels' collection time and refresh age.

Do not read environment variables or application log contents by default. Commands and paths may contain sensitive information, so exports require an explicit command.

### Noninteractive CLI

Planned interface:

- `instmon`: interactive monitor on a terminal; one plain snapshot when output is redirected.
- `instmon list`: one process snapshot.
- `instmon inspect <pid>`: one detailed process snapshot, recording its identity before collection.
- Shared options: `--sysmon` (interactive only), `--interval <seconds>`, `--sort <field>`, `--reverse`, `--user <name-or-uid>`, `--filter <text>`, `--json`, `--help`, and `--version`, where applicable.
- `--interval` controls interactive refresh; default 1 second, minimum 0.25 seconds. Reject it for commands that do not refresh.
- JSON includes a schema version, timestamps, units, process identity, nullable values, and field availability. JSON contains no terminal control sequences.
- Exit codes: 0 for a successful collection (including explicitly reported partial field availability), 1 for fatal runtime failure, and 2 for invalid arguments. An absent requested PID is a runtime failure. Help and version exit successfully.

## 3. Technology choices

- C17 for implementation, with explicit allocation ownership and checked conversions.
- POSIX/Linux APIs and procfs for collection; do not spawn `ps`, `top`, or shell commands as the data source.
- `ncursesw` for keyboard input, scrolling, colors, and terminal resizing. Core collection and plain/JSON output must work without terminal initialization.
- Make and GCC/Clang for the initial build; use `pkg-config` for ncursesw discovery. Add clear dependency installation instructions per supported distribution during implementation.
- A small internal JSON writer with focused escaping tests, or one documented JSON dependency if its maintenance cost is justified before that phase. Freeze that choice before implementing export.
- No database, daemon, networking service, or root requirement for the first release. Keep recent history in bounded memory.

## 4. Architecture

### Data flow

`CLI options → controller → platform collector → snapshots → metric calculations → filters/sorting → TUI or plain/JSON renderer`

The controller owns refresh scheduling, current selection, history, and lifecycle. The collector returns normalized data without terminal formatting. Renderers consume snapshots and never read procfs themselves.

### Proposed source layout

| Path | Responsibility |
| --- | --- |
| `main.c` | Parse startup outcome and dispatch; keep entry point small |
| `include/instmon/` | Shared types and module interfaces |
| `src/cli.c` | Argument validation, help, output mode selection |
| `src/controller.c` | Refresh loop, selection, panel requests, shutdown |
| `src/model.c` | Snapshot allocation, ownership, identity, availability |
| `src/metrics.c` | Counter deltas, percentages, rates, bounded histories |
| `src/query.c` | Filtering, sorting, parent/child indexing |
| `src/platform/platform.h` | Backend contract and capability flags |
| `src/platform/linux/` | Process discovery, parsing, system and detail collectors |
| `src/sysmon.c`, `include/sysmon.h` | Cached system information, diagnostics, asynchronous optional systemctl query |
| `src/ui/` | Overview, detail panels, help, input and layout |
| `src/output/` | Plain tables and versioned JSON |
| `src/util/` | Checked parsing, strings, time, error handling |
| `tests/fixtures/` | Sanitized procfs examples and malformed inputs |
| `tests/` | Parser, metrics, lifecycle and output tests |
| `Makefile` | Build, check, sanitizer and clean targets |
| `README.md` | Installation, usage, limitations and metric definitions |

### Core data structures and contracts

- **ProcessIdentity:** PID plus start ticks; history and selection use both. Revalidate identity around detail collection so PID reuse cannot mix two instances.
- **SystemSnapshot:** sample time, total/available RAM, CPU topology, uptime, and sample status.
- **ProcessSnapshot:** identity, metadata, raw cumulative counters, memory gauges, and per-field availability.
- **DetailSnapshot:** identity, panel data, collection timestamps, truncation indicators, and field errors.
- **DerivedMetrics:** values computed from successive valid samples of the same identity.
- **HistoryStore:** bounded ring buffers, initially 60 samples for the selected process only. Remove stale identities and reset history when selection changes.
- **ViewState:** filters, sort, selected identity, panel, scroll offset, terminal size, and optional error message.

The backend exposes system sampling, process enumeration, basic collection, selected detail collection, and capability discovery. It returns typed statuses rather than printing. Snapshot owners free all nested buffers; renderers borrow immutable data only for the duration of a render.

### Collection and scheduling

Use a single-threaded event loop initially. Use monotonic time for deadlines and rate calculations, and wall-clock time only for display timestamps. Input remains responsive through timed terminal polling and incremental work batches.

Collect lightweight process counters each refresh. Load file lists, thread details, mappings summaries, and connection data only for the selected panel. Cache expensive results for five seconds initially, show their age, cap entries and bytes read, and cancel pending work when selection changes. Reassess a worker thread only if measured latency remains unacceptable.

Refresh deadlines must not drift by adding collection duration to every interval. If collection overruns, skip missed deadlines rather than queueing refreshes. A process list is a best-effort observation over a collection window, not an atomic system-wide snapshot.

## 5. Linux collection and metric definitions

Implementation reference: [Linux kernel procfs documentation](https://www.kernel.org/doc/html/latest/filesystems/proc.html). Check individual field semantics against this reference during implementation.

### Sources

| Source | Intended use |
| --- | --- |
| `/proc` numeric directories | Visible PID discovery |
| `/proc/<pid>/stat` | Identity, state, CPU counters, parent, thread count and start ticks |
| `/proc/<pid>/status`, `statm` | Basic memory and process status |
| `/proc/<pid>/cmdline`, `exe`, `cwd` | Command and executable/directory metadata |
| `/proc/<pid>/io` | Process I/O counters where accessible |
| `/proc/<pid>/task/` | Selected process thread details |
| `/proc/<pid>/fd/` | Descriptor targets and socket inode ownership |
| `/proc/<pid>/smaps_rollup` | On-demand proportional memory when supported and accessible |
| `/proc/<pid>/limits` | Resource limits |
| `/proc/<pid>/net/` | Network-namespace socket tables, correlated to owned socket inodes |
| `/proc/meminfo`, `/proc/uptime`, `/proc/stat` | System memory, uptime and CPU context |

Read the relevant fields with bounded parsers, not assumptions about fixed buffer sizes or simple whitespace splitting. Process names in `stat` can contain spaces and parentheses; command arguments are NUL-separated. Sanitize control characters before displaying any process-controlled text. Handle changing descriptors, truncated reads, and missing optional files.

### Definitions

- **RAM:** resident set size (RSS), displayed in MiB. RSS is an approximate resident-memory measurement and can count shared pages in more than one process; summing it is not unique physical RAM use.
- **Virtual memory:** address-space size, labeled separately from RAM.
- **PSS:** optional proportional attribution of shared resident pages; collect only on demand, with no fabricated fallback.
- **Memory percentage:** process RSS bytes divided by host total physical-memory bytes, multiplied by 100. Label the host denominator; it is not a container memory-limit percentage.
- **CPU percentage:** delta of process user plus system CPU ticks divided by clock ticks per second and monotonic elapsed seconds, multiplied by 100. One fully busy logical CPU is 100%; multithreaded processes can exceed 100%. Do not add child CPU counters.
- **I/O rates:** counter delta divided by elapsed monotonic seconds. Keep storage read/write bytes distinct from syscall character counters; neither is per-process network throughput.
- **Counter warm-up:** first samples have no rate. Reset, decreased counters, invalid elapsed time, or changed identity invalidate the delta.
- **Elapsed lifetime:** derive from boot-relative start time using system clock-tick frequency and uptime, with checked conversions.
- Use runtime page size and clock-tick frequency, checked 64-bit counters, byte-based internal storage, and explicit display units.

Network tables describe a namespace, not exclusively the selected process. Join socket inodes from that process's descriptors before presenting ownership. Connections may be shared or disappear during collection. Accurate per-process network byte rates are deferred.

Activity summaries must be factual: for example, “Sleeping; low CPU; storage writes observed in last interval.” Do not infer a deadlock from sleeping or a memory leak from one high RSS reading.

## 6. Step-by-step implementation plan

### Step 1 — Establish the build and boundaries

1. Set up the source layout, C17 compiler flags, debug/release builds, and Make targets.
2. Define snapshot types, ownership rules, field status enum, and backend interfaces.
3. Implement help/version and argument validation before terminal initialization.

**Done when:** a minimal executable builds with GCC and Clang; invalid input fails clearly; plain output does not require a terminal.

### Step 2 — Discover processes and collect memory

1. Implement bounded procfs reads and checked numeric parsing using a configurable procfs root for fixtures.
2. Read system RAM and enumerate visible processes.
3. Capture identity, name, owner, state, RSS, virtual memory, and thread count.
4. Render `instmon list` as a plain table, with explicit unavailable values.

**Done when:** process creation/exit during enumeration is harmless, RSS uses correct units, and restricted fields do not hide otherwise visible rows.

### Step 3 — Add time-based metrics

1. Record monotonic timestamps for samples and match identities between collections.
2. Compute CPU, I/O rates, memory percentage and elapsed lifetime.
3. Implement sorting, filters and a process relationship index.
4. Add selected-process history with fixed capacity.

**Done when:** first samples show warming-up rates, PID reuse resets state, and a controlled CPU workload produces the expected trend.

### Step 4 — Build the interactive overview

1. Initialize ncursesw with reliable cleanup and terminal restoration.
2. Add system summary, scrollable table, selection, search, sort and help.
3. Handle resizing, tiny terminals, empty filters, long names and terminal input errors.
4. Schedule collection in bounded batches while preserving keyboard responsiveness.

**Done when:** users can find and select a process while the table refreshes, and normal exits or handled termination signals restore the terminal.

### Step 5 — Implement core diagnostics

1. Add overview, memory, CPU/thread and I/O panels.
2. Fetch expensive fields lazily and display collection age.
3. Revalidate selected identity and handle exit/reuse during reads.
4. Include recent resource history as compact terminal sparklines or numeric trends.

**Done when:** selecting a process explains its observed resource behavior and loss of access does not crash or change the selected identity.

### Step 6 — Add resource inspection

1. Implement bounded descriptor and thread lists, resource limits, and relationships.
2. Add on-demand PSS and namespace-aware socket correlation.
3. Make every long list scrollable and disclose truncation or collection limits.

**Done when:** unavailable diagnostics remain usable, socket ownership is filtered correctly, and switching panels cancels obsolete collection work.

### Step 7 — Complete inspection and export commands

1. Implement `inspect <pid>` with the same collector and metric definitions as the TUI.
2. Finalize and document JSON schema version 1, including availability and timestamp semantics.
3. Implement correct JSON escaping, redirected-output behavior, and stable exit codes.
4. Keep diagnostic messages on stderr and machine-readable output on stdout.

**Done when:** an independent JSON parser accepts output, special characters survive round trips, and a noninteractive inspection identifies its collection window and limitations.

### Step 8 — Verify correctness and harden failures

1. Test valid, malformed, missing, oversized and permission-restricted fixture data.
2. Test rate arithmetic, counter resets, PID reuse, process churn, disappearing selected processes, sorting stability and filter behavior.
3. Run AddressSanitizer and UndefinedBehaviorSanitizer builds and exercise cleanup paths.
4. Compare a quiet controlled process against `ps`/`top` within sampling tolerances; investigate definition differences before treating discrepancies as defects.
5. Exercise TUI resizing, signal handling, EOF, narrow screens, control characters and Unicode text.

**Done when:** checks pass, long-running smoke sessions show bounded memory and descriptor use, and restricted access yields explicit statuses.

### Step 9 — Measure overhead and prepare release

1. Benchmark refresh duration, input latency, monitor CPU, and RSS on a documented laptop and process count.
2. Initial targets at a one-second interval and roughly 500 visible processes: median monitor CPU below 2% of one logical CPU, RSS below 50 MiB, and typical input response below 100 ms. Treat these as measured goals, not guarantees; report hardware and methodology.
3. Tune collection budgets and caches only where measurements identify a problem. Disclose overruns instead of implying the interval was met.
4. Write installation, usage, permissions, metric definitions, keyboard help, supported platform and troubleshooting documentation.
5. Choose a license, create a changelog, and tag a release after validation.

**Done when:** another user can build, launch, select a process, interpret RAM correctly, and export diagnostics using only the README.

## 7. Release acceptance checklist

- [ ] Every visible process can appear, including duplicate executable names.
- [ ] RAM and CPU have documented units and denominators.
- [ ] Selected-instance identity survives sorting and refreshes.
- [ ] Core diagnostic panels work and clearly report inaccessible fields.
- [ ] Process churn, PID reuse and changing permissions are handled.
- [ ] No shell commands or application-controlled escape sequences reach execution or terminal interpretation.
- [ ] Collection work, stored history and dynamic lists are bounded.
- [ ] Plain output and JSON work without an interactive terminal.
- [ ] Terminal restoration, compiler checks and sanitizer checks pass.
- [ ] Documentation describes measured overhead and platform limits.

## 8. Future extensions

After the Linux release, evaluate logical application grouping with explicit membership rules, alert thresholds, longer opt-in history, cgroup/container views, and macOS/Windows backends. Keep capability differences visible. Trace collection, stack sampling and process-control commands each require a separate design because they change privileges, overhead, and interaction with the monitored application.

## 9. Implemented Sysmon extension (v0.2.0)

The system view shares `src/ui.c` rendering, document scrolling, footer and event
loop. `instmon` retains its process-table default; `--sysmon` starts in Sysmon.
`s` opens Sysmon, `m` returns to the main table, and Shift+S cycles sort order
(the user-approved resolution of the original `s` key conflict).

`src/sysmon.c` caches hardware data once, refreshes sensor/load/root-filesystem
information every five seconds only when visible, and reuses existing RAM/uptime
snapshots. Sources are os-release, uname, procfs, sysfs and statvfs. GPU reporting
uses readable DRM vendor/device IDs; storage lists visible physical devices and
root-filesystem diagnostics. Unknown hardware remains unavailable.

Failed system-scope services are the one optional external query: exec systemctl
without a shell, poll a nonblocking pipe in the existing UI loop, cap output,
kill after two seconds, reap on completion/exit, and cache for 30 seconds.
Process collection itself continues to use no subprocesses. No extra libraries
or threads are added. Collector fixtures and pseudo-terminal navigation tests
cover the new view, absence of hardware/systemd, query failures and cleanup.

## 10. Implemented process management (v0.3.0)

Home-screen k captures the selected process identity in a confirmation state inside
`src/ui.c`; Enter confirms and Escape cancels. No new view, library or event loop is
introduced. Refreshes continue, but cannot change the pending target. Escape also
cancels search or clears selection; missing selected identities never fall back to
neighboring rows. `src/metrics.c` applies the same PID/name/command search to CLI and TUI.

`process_terminate` in `src/proc.c` opens a pidfd, re-reads start ticks, refuses
changed identities and exited processes, then sends SIGTERM through that same
handle. It closes the handle on every path. Unsupported/blocked pidfd operations
fail without a numeric-PID fallback. No escalation, retry, automatic termination,
elevation or process-group signaling is implemented. A successful signal does not
promise application exit. Sysmon's existing private query-child cleanup is unchanged.

Tests exercise disposable child processes, injected syscall errors, mismatched
identities, PID search, stable selection, cancellation, target exit during a prompt,
and an ignored SIGTERM. The existing release matrix runs these alongside all
monitoring and Sysmon regression tests on Linux x86-64 and ARM64.
