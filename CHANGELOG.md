# Changelog

## 0.2.0

- Add a System Monitor view with OS, kernel, hostname, uptime, CPU topology,
  GPU device IDs, RAM and physical storage capacities.
- Add CPU temperature, RAM usage, root-filesystem usage, load averages and
  asynchronous failed-systemd-service diagnostics with graceful fallbacks.
- Add `--sysmon`, `s` for Sysmon and `m` for the main monitor. Move sort cycling
  to Shift+S to resolve the shortcut conflict; preserve other controls and state.
- Cache hardware and diagnostic reads; bound service output and query duration.
- Add hardware/systemd fixture tests and both-view terminal regression tests.

## 0.1.0

- Initial Linux process monitor, selectable diagnostics, plain and JSON output.
- Prebuilt x86-64/ARM64 releases and checksum-verified one-command installation.
