System Monitor arrives in InstanceMonitor v0.2.0.

- Start with `instmon --sysmon`, press `s` for Sysmon, or `m` for the main monitor.
- Inspect OS, kernel, hostname, uptime, CPU/topology, GPU IDs, RAM and storage.
- View CPU temperature, RAM/root-filesystem usage, load and failed system services.
- Hardware and diagnostics are cached; optional systemctl queries are asynchronous,
  bounded and timed out. Missing hardware or systemd is reported gracefully.
- Sort cycling now uses **Shift+S**; other shortcuts and process features remain.

Install or upgrade without compiling:

```sh
curl -fsSL https://raw.githubusercontent.com/AbhinavTheCoder-star/InstanceMonitor/main/install.sh | sh
instmon --sysmon
```

Includes static Linux x86-64 and ARM64 binaries with SHA-256 checksums. The installer
uses `/usr/local/bin` and sudo if needed; pass `sh -s -- --prefix "$HOME/.local"`
for a user-only installation. Linux `/proc` and a terminal database are required;
`systemctl` is optional. Sysmon is interactive only. See README for measurement
scope and unavailable-data behavior.
