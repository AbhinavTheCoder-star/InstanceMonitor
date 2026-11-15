InstanceMonitor
A C-based CLI for monitoring running processes, tracking RAM and CPU usage, and exploring application diagnostics from your terminal.
Overview
InstanceMonitor aims to make it easier to understand what is running on your laptop and how those processes use system resources. Browse running instances, compare resource usage, and select a process to inspect its activity and diagnostics.

An instance refers to an individual operating-system process. Multiple processes running the same application appear separately.
Project status
Currently in the planning stage. The features below describe the intended application and are not yet implemented.

The initial release will target Linux.
Planned features
- Live process overview: View process IDs, names, owners, states, CPU usage, RAM usage, thread counts, and uptime.
- Memory monitoring: Inspect resident RAM, virtual memory, swap, and additional memory details where available.
- Process diagnostics: Select an instance to explore its command, executable path, working directory, resource limits, and recent activity.
- CPU and thread inspection: Track CPU consumption and inspect individual threads.
- I/O monitoring: View read/write counters and transfer rates.
- Open files and connections: Inspect accessible file descriptors and associated network sockets.
- Search, filtering, and sorting: Find processes by name or command and compare them by resource usage.
- Process tree: Explore parent and child relationships.
- Terminal interface: Navigate an interactive overview and diagnostic panels using the keyboard.
- Plain-text and JSON output: Collect snapshots for scripts and further analysis.
Planned usage
The executable will be named instmon.
# Open the interactive monitor
instmon

# Print a process snapshot
instmon list

# Inspect a specific process
instmon inspect <pid>

# Export a snapshot as JSON
instmon list --json
These commands describe the planned interface and are not available yet.
Technology
- Language: C17
- Initial platform: Linux
- Data collection: Linux procfs and POSIX/Linux APIs
- Terminal interface: ncursesw
- Build tools: Make and GCC or Clang
The application will operate locally, with no background service or database required.
Understanding the data
RAM usage will primarily refer to resident set size (RSS). Shared memory may appear in multiple processes, so adding their RSS values does not represent unique physical memory usage.

CPU usage will use 100% to represent one fully occupied logical CPU. Multithreaded processes may exceed 100%.

Process state, CPU activity, and I/O counters can describe observable behavior. They cannot explain an application's internal intent or determine which business operation it is performing.

Some diagnostics may be restricted by operating-system permissions. InstanceMonitor will identify unavailable fields explicitly.
Development plan
1. Establish the C project structure and build system.
2. Implement process discovery and memory collection.
3. Add CPU measurements, I/O rates, filtering, and sorting.
4. Build the interactive process overview.
5. Implement detailed diagnostic panels.
6. Add file, connection, thread, and resource-limit inspection.
7. Complete plain-text and JSON output.
8. Validate correctness, failure handling, and resource usage.
9. Document installation and prepare the first release.
See [instructions.md](instructions.md) for the full architecture, implementation steps, and acceptance criteria.
Scope
The initial release will provide read-only monitoring of processes visible to the current user. Process termination, debugging attachment, syscall tracing, remote monitoring, and macOS or Windows support are outside the initial scope.
Building and installation
Build and installation instructions will be added when the initial implementation is ready.
