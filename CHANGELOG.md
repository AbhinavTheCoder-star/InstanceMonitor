# Changelog

## 0.3.0

- Add home-screen SIGTERM requests with name/PID confirmation and Escape cancellation.
- Verify process identity and signal through Linux PID handles to prevent PID-reuse
  races; report permission, exited-process and unavailable-kernel errors.
- Keep selection stable through refresh/sort and never substitute a neighboring
  process after the target exits. Escape deselects until explicit navigation.
- Add PID substring search alongside existing name/command matching.
- Use k for termination on the home screen; retain k scrolling in diagnostic views.
- Add child-process, injected-error and terminal tests for management and cancellation.

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
