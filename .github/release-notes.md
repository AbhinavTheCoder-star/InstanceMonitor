Run InstanceMonitor on Linux without compiling anything.

```sh
curl -fsSL https://raw.githubusercontent.com/AbhinavTheCoder-star/InstanceMonitor/main/install.sh | sh
instmon
```

The installer detects x86-64 or ARM64, verifies the archive's SHA-256 checksum,
and installs to `/usr/local/bin` (using sudo if needed). For a user-only install,
pass `sh -s -- --prefix "$HOME/.local"` instead of `sh`; ensure `~/.local/bin` is on PATH.

Alternatively, download the archive matching your CPU, verify its accompanying
`.sha256` file with `sha256sum -c`, extract it, and run `./instmon`.

Binaries statically link musl and ncurses; no compiler or ncurses shared library
is required. Interactive mode uses the system's terminal database (`terminfo`),
which is normally already installed on desktop Linux. Linux `/proc` is required.
macOS, Windows, 32-bit systems, and other CPU architectures are not supported.

To upgrade, rerun the installer. To uninstall a default installation, run
`sudo rm /usr/local/bin/instmon`.

This is the initial implementation: process trees, per-thread CPU metrics,
decoded connections and history charts remain planned. See the README for details.
