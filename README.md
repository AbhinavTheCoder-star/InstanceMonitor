# InstanceMonitor

A C17 terminal tool for monitoring Linux processes, tracking RAM and CPU usage,
and inspecting application diagnostics. Each instance is one process, identified
by its PID and start time. Monitoring runs without root; optional process
termination requires explicit confirmation and follows normal OS permissions.

## Status

Version 0.3.0 adds confirmed process termination to the existing home screen. It provides:

- System information and diagnostics in Sysmon (`instmon --sysmon`).
- Live process table with owner, state, CPU, RAM, memory percentage and threads.
- Name/command/PID search, owner filtering, sorting and stable process selection.
- Confirmed, identity-checked SIGTERM requests from the home screen.
- Selectable overview, status, memory, open-descriptor and resource-limit panels.
- Executable path, working directory, parent PID, age and storage-I/O counters.
- On-demand proportional memory data when kernel support and permissions allow.
- Plain-text snapshots and versioned JSON export.
- Bounded reads, PID-reuse checks and explicit unavailable/permission messages.

Process trees, per-thread CPU metrics, decoded network endpoints, resource-history
charts, incremental collection and performance benchmarking remain on the roadmap.
Socket inodes currently appear in the descriptor panel; they are not decoded into
connections. See [instructions.md](instructions.md) for the full design.

## Install — no build required

Linux x86-64 and ARM64 users can install the latest prebuilt release with:

```sh
curl -fsSL https://raw.githubusercontent.com/AbhinavTheCoder-star/InstanceMonitor/main/install.sh | sh
instmon
```

The installer downloads the matching binary, verifies its SHA-256 checksum, and
installs it to `/usr/local/bin`. It requests sudo only if that directory requires
it. Requirements: curl, tar, sha256sum and standard Linux utilities; no compiler
or ncurses shared libraries are needed. Interactive mode uses your system's
terminal database (terminfo), normally present on desktop Linux.

For a user-only installation:

```sh
curl -fsSL https://raw.githubusercontent.com/AbhinavTheCoder-star/InstanceMonitor/main/install.sh | sh -s -- --prefix "$HOME/.local"
```

If `~/.local/bin` is not already on PATH, the installer prints the command to add
it. Put that command in your shell's startup file to keep it across sessions.

Prefer a manual download? Open [Releases](https://github.com/AbhinavTheCoder-star/InstanceMonitor/releases/latest),
download the archive for your CPU and its `.sha256` file into the same directory,
verify with `sha256sum -c <archive>.sha256`, extract it and run `./instmon`.
The checksum detects corrupt or mismatched downloads; it is not a separate signature.

Rerun the installer to upgrade. Pin a version by passing `sh -s -- --version v0.3.0`
instead of `sh`. To uninstall the default installation:

```sh
sudo rm /usr/local/bin/instmon
```

For a user-only installation, remove `~/.local/bin/instmon` instead. macOS,
Windows, 32-bit systems and other CPU architectures are not supported.

## Build from source (developers)

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
instmon                         # Existing process monitor
instmon --sysmon                # System information and diagnostics
instmon list                    # One plain snapshot
instmon inspect 1234            # Detailed inspection of PID 1234
instmon list --sort ram
instmon --user "$USER" --interval 0.5
instmon list --filter firefox --json
instmon inspect 1234 --json > diagnostics.json
instmon --help
```

Redirecting the default command produces one plain snapshot. `--json` always
selects noninteractive output. `--interval` is interactive only, from 0.25 to
3600 seconds. Default refresh is one second; details refresh every five seconds.
`inspect` does not accept search or owner filters.

| Key | Action |
| --- | --- |
| Up/Down arrows | Select a process; scroll within diagnostics |
| k | Request termination on the home screen; scroll up in diagnostics |
| j | Move down or scroll down |
| Enter | Inspect selection; confirm SIGTERM when prompted |
| Tab | Next diagnostic panel |
| Escape | Cancel confirmation, search or selection; return from diagnostics |
| / | Search names, commands and PIDs; Enter finishes, Escape clears |
| s | Open System Monitor (Sysmon) |
| m | Return to the main process monitor |
| Shift+S | Cycle RAM, CPU, name and PID sorting |
| r | Reverse sorting |
| ? | Toggle help |
| q | Quit, except while typing a search |

A selected process that exits remains identifiable in the detail view. Cached
information is labeled as last observed; a new process with a reused PID is never
silently substituted.

## Managing a process

Use the arrow keys to highlight a process on the home screen. Press `k` to request
termination. The confirmation shows its name and PID: **Enter sends SIGTERM**,
while **Escape cancels**. No signal is sent just by selecting a row or pressing k.
The prompt stays on the home screen and keeps its original target across refreshes;
other navigation keys are ignored until you confirm or cancel. A terminal too small
to show the prompt cancels the request.

SIGTERM asks the application to exit gracefully. It may take time or be ignored;
a success message means the signal was sent, not that the application exited.
Instmon never escalates a selected process to SIGKILL, retries a failed request,
or requests elevated privileges. Permission failures and exited/changed processes
are reported on the home screen.

Immediately before signaling, Instmon verifies the process start time and uses a
Linux PID handle to avoid sending SIGTERM to a reused PID. Termination requires
Linux 5.3+ with `pidfd_open` and `pidfd_send_signal` permitted by the environment.
If these interfaces are unsupported or blocked, no signal is sent; monitoring
continues normally. No fallback to signaling a numeric PID is used.

Search with `/`, type a name or PID (case-insensitive literal substring matching),
and press Enter to finish. Existing command-line searching also matches PIDs.
Escape clears a search and deselects the row; Escape on the home screen deselects
without changing the filter. Use arrows to select again. A selected process that
leaves the list is not silently replaced by a neighboring row. The `k` key retains
its upward-scroll behavior in diagnostics and Sysmon, and is literal text in search.

## System Monitor (Sysmon)

Run `instmon --sysmon`, or press `s` from the process monitor. Press `m` to return
to the main process table. Filters, selection and sort order are retained. Sort
cycling uses **Shift+S**; other existing keys retain their behavior. While typing
in a search, `s` and `m` remain literal search characters. Escape also returns to
the process table; arrows or j/k scroll the Sysmon document on smaller terminals.

Sysmon displays OS, kernel, hostname, uptime, CPU model, online physical-core and
thread counts, GPU vendor/device IDs, total RAM and visible physical block-device
capacities. GPU marketing names are not guessed and no PCI database is downloaded.
Missing or restricted hardware data is labeled unavailable.

Diagnostics include maximum readable CPU temperature from recognized CPU sensors,
RAM used (`MemTotal - MemAvailable`), root-filesystem disk usage, system load
(1/5/15-minute averages), and failed **system-scope** systemd services. Disk usage
covers `/`, not every mount; physical capacities are shown individually and are
not summed across RAID or other storage layers. Disk used excludes free blocks;
available space can be smaller because of filesystem reservations. Load averages
are queue lengths, not CPU percentages.

Hardware information is cached for the session. Temperature, disk and load refresh
every five seconds while Sysmon is visible; RAM and uptime reuse the process
snapshot. CPU topology reflects online CPUs when the view is first opened.
Failed-service checks are cached for 30 seconds and use an optional, read-only
`systemctl` subprocess, without a shell. Queries are asynchronous with a two-second
timeout and bounded output. Missing systemd/systemctl or inaccessible service
managers produce an unavailable message, not a false “no failures” result.
No new libraries, daemon or background threads are required.

`--sysmon` requires an interactive terminal and cannot be combined with `list`,
`inspect` or `--json`; those output modes continue to describe processes.

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
implementation to keep terminal and JSON handling predictable.

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
make check       # Core, CLI, JSON, both TUI views, installer and Sysmon tests
make sanitize    # Rebuild and run with AddressSanitizer + UndefinedBehaviorSanitizer
make clean
make             # Return to the normal build
```

Collection lives in `src/proc.c`, metric calculations and queries in
`src/metrics.c`, argument parsing in `src/cli.c`, output in `src/output.c`, and
interactive control/rendering in `src/ui.c`. Shared types and ownership contracts
are in `include/instmon.h`. `main.c` selects the execution mode and owns one-shot
snapshot cleanup. The implementation uses ncursesw and a small streaming JSON
writer; process data comes directly from Linux interfaces. Sysmon alone optionally queries
`systemctl` without a shell. Its collector and bounded child lifecycle live in
`src/sysmon.c`, with contracts in `include/sysmon.h`.

## Publishing releases

Maintainers publish a new version by updating the version in `src/cli.c`, then
pushing a matching tag such as `v0.3.0`. The release workflow builds native x86-64
and ARM64 static binaries in Alpine Linux, runs the core/CLI/JSON/terminal tests,
and uploads archives, SHA-256 checksums and the installer to GitHub Releases only
when both architecture builds succeed. Build artifacts contain third-party notices.

```sh
git tag v0.3.0
git push origin v0.3.0
```

Do not reuse published version tags. Run `python3 tests/test_install.py` to check
the installer without network access or writing into system directories.
