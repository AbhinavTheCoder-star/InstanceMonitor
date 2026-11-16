InstanceMonitor v0.3.0 adds simple process management to the existing home screen.

- Select with Up/Down; press `k` for a confirmation showing process name and PID.
- Enter sends SIGTERM; Escape cancels. No automatic termination or SIGKILL escalation.
- Start-time verification and Linux PID handles prevent signaling a reused PID.
- Permission, exited-process and unavailable-kernel errors are shown in place.
- `/` searches names, commands and PIDs. Escape cancels search or clears selection.
- Selection and pending targets stay stable across refreshes; Sysmon and diagnostics remain available.

Install or upgrade:

```sh
curl -fsSL https://raw.githubusercontent.com/AbhinavTheCoder-star/InstanceMonitor/main/install.sh | sh
instmon
```

Static Linux x86-64 and ARM64 binaries and SHA-256 checksums are attached.
Termination requires Linux 5.3+ and permitted pidfd syscalls; unsupported or blocked
environments send no signal and retain monitoring. SIGTERM delivery does not
guarantee exit. No new dependencies. See README for shortcuts and details.
