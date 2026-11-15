# InstanceMonitor

A C17 terminal tool for monitoring Linux processes, tracking RAM and CPU usage,
and inspecting application diagnostics. Each instance is one process, identified
by its PID and start time. The monitor is read-only and runs without root.

## Status

Version 0.1 is an initial working implementation. It provides:

- Live process table with owner, state, CPU, RAM, memory percentage and threads.
- Name/command search, owner filtering, sorting and stable process selection.
- Selectable overview, status, memory, open-descriptor and resource-limit panels.
- Executable path, working directory, parent PID, age and storage-I/O counters.
- On-demand proportional memory data when kernel support and permissions allow.
- Plain-text snapshots and versioned JSON export.
- Bounded reads, PID-reuse checks and explicit unavailable/permission messages.

Process trees, per-thread CPU metrics, decoded network endpoints, resource-history
charts, incremental collection and performance benchmarking remain on the roadmap.
Socket inodes currently appear in the descriptor panel; they are not decoded into
connections. See [instructions.md](instructions.md) for the full design.

## Build

Requires Linux with `/proc` mounted, Make, pkg-config, GCC or Clang, and ncursesw
headers/libraries. Python 3 is needed only for integration tests.

On Debian/Ubuntu, install `build-essential pkg-config libncurses-dev python3`.
On Arch/CachyOS, install `base-devel pkgconf ncurses python`.

```sh
make
./instmon
```

For Clang, use `make clean && make CC=clang`.

## Usage

```sh
./instmon                         # Interactive monitor
./instmon list                    # One plain snapshot
./instmon inspect 1234            # Detailed inspection of PID 1234
./instmon list --sort ram
./instmon --user "$USER" --interval 0.5
./instmon list --filter firefox --json
./instmon inspect 1234 --json > diagnostics.json
./instmon --help
```

Redirecting the default command produces one plain snapshot. `--json` always
selects noninteractive output. `--interval` is interactive only, from 0.25 to
3600 seconds. Default refresh is one second; details refresh every five seconds.
`inspect` does not accept search or owner filters.

| Key | Action |
| --- | --- |
| Arrows or j/k | Move selection; scroll within a diagnostic panel |
| Enter | Inspect selected process |
| Tab | Next diagnostic panel |
| Escape | Return to process table |
| / | Search names and commands; Enter finishes, Escape clears |
| s | Cycle RAM, CPU, name and PID sorting |
| r | Reverse sorting |
| ? | Toggle help |
| q | Quit, except while typing a search |

A selected process that exits remains identifiable in the detail view. Cached
information is labeled as last observed; a new process with a reused PID is never
silently substituted.

## Reading the measurements

- **RAM** is RSS in MiB. Shared pages can count in several processes; summed RSS
  is not unique physical memory consumption. RSS is approximate.
- **RAM percentage** uses host physical RAM as its denominator, not a container limit.
- **Virtual memory** describes address space and is separate from resident RAM.
- **CPU** uses 100% for one logical CPU; multithreaded processes can exceed 100%.
  First samples show warming up. One-shot commands do not collect a second sample,
  so their CPU percentage is unavailable.
- **I/O** means storage read/write counters, not network traffic. Rates are computed
  internally between valid samples; the current views expose cumulative counters.
- **State** uses Linux letters such as R (running), S (sleeping), D (uninterruptible
  sleep), T (stopped), and Z (zombie). State does not reveal application intent.

Only processes visible in the current PID namespace can be collected. Some fields
are restricted by permissions. An inaccessible or rapidly exiting process may be
omitted if its identity cannot be established safely. Collection spans a time
window rather than an atomic snapshot. Slow collection can delay keyboard input;
refresh duration is displayed. Non-ASCII process text is replaced with `?` in this
initial version to keep terminal and JSON handling predictable.

Commands and paths may contain private information. Export occurs only when you
request it; environment variables and application log contents are not read.

## JSON and exit codes

Schema version 1 includes collection time, duration, units, host memory and process
records. Memory counters are bytes, age is seconds, `sampled_at_unix` is Unix time,
and start ticks identify a process lifetime within this boot. Missing CPU values
are `null` with `cpu_status`; missing I/O counters are `null` with `io_status`.
Detail text contains bracketed availability messages and truncation markers.
`details.identity_valid=false` means detail fields were discarded after exit or
identity change. Raw diagnostic text is kernel-dependent, not a stable sub-schema.

Exit codes: `0` success (possibly partial fields), `1` runtime failure or missing
requested PID, `2` invalid arguments. Diagnostics go to stderr; results go to stdout.

## Development and checks

```sh
make check       # Parser, metrics, CLI, JSON and pseudo-terminal tests
make sanitize    # Rebuild and run with AddressSanitizer + UndefinedBehaviorSanitizer
make clean
make             # Return to the normal build
```

Collection lives in `src/proc.c`, metric calculations and queries in
`src/metrics.c`, argument parsing in `src/cli.c`, output in `src/output.c`, and
interactive control/rendering in `src/ui.c`. Shared types and ownership contracts
are in `include/instmon.h`. `main.c` selects the execution mode and owns one-shot
snapshot cleanup. The implementation uses ncursesw and a small streaming JSON
writer; it does not launch shell commands to collect process data.
