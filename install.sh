#!/bin/sh
# Download a published binary, verify its digest, and install it on PATH.
# This installer never compiles source or installs build dependencies.
set -eu

repository=AbhinavTheCoder-star/InstanceMonitor
prefix=/usr/local
version=latest
usage() {
    echo 'Usage: install.sh [--version vX.Y.Z] [--prefix DIRECTORY]'
    echo 'Default: latest release, installed to /usr/local/bin/instmon (sudo if needed).'
}
fail() { echo "instmon installer: $*" >&2; exit 1; }
while [ "$#" -gt 0 ]; do
    case "$1" in
        --version|--prefix)
            option=$1
            [ "$#" -ge 2 ] || fail "missing value for $option"
            case "$option" in --version) version=$2 ;; --prefix) prefix=$2 ;; esac
            shift 2 ;;
        --help) usage; exit 0 ;;
        *) fail "unknown option: $1" ;;
    esac
done
case "$prefix" in /*) ;; *) fail '--prefix must be an absolute path' ;; esac
[ "$(uname -s)" = Linux ] || fail 'only Linux is supported'
case "$(uname -m)" in
    x86_64|amd64) architecture=x86_64 ;;
    aarch64|arm64) architecture=aarch64 ;;
    *) fail "unsupported CPU architecture: $(uname -m)" ;;
esac
for utility in curl tar sha256sum mktemp install; do
    command -v "$utility" >/dev/null 2>&1 || fail "required utility not found: $utility"
done

# Resolve latest once. Both files must come from exactly the same release even
# if a maintainer publishes a newer release while this installation is running.
if [ "$version" = latest ]; then
    resolved=$(curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' \
        --connect-timeout 15 --max-time 60 --output /dev/null --write-out '%{url_effective}' \
        "https://github.com/$repository/releases/latest")
    version=${resolved##*/}
fi
# Permit only the stable tag format understood by this installer; never turn a
# caller-supplied version into an arbitrary path or an option to another tool.
printf '%s\n' "$version" | LC_ALL=C grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$' || fail 'version must look like v0.1.0'
asset=instmon-linux-$architecture.tar.gz
base=https://github.com/$repository/releases/download/$version
scratch=$(mktemp -d)
staged=
privileged=no
cleanup() {
    rm -rf "$scratch"
    if [ -n "$staged" ]; then
        if [ "$privileged" = yes ]; then sudo rm -f "$staged"; else rm -f "$staged"; fi
    fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
fetch() {
    curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' \
        --connect-timeout 15 --max-time 180 --retry 3 "$1" --output "$2"
}
echo "Downloading InstanceMonitor $version for Linux $architecture..."
fetch "$base/$asset" "$scratch/$asset"
fetch "$base/$asset.sha256" "$scratch/digest"
# Do not interpret downloaded checksum filenames as local filesystem paths.
digest=$(cut -d ' ' -f 1 "$scratch/digest")
[ "${#digest}" -eq 64 ] || fail 'invalid SHA-256 checksum'
case "$digest" in *[!a-fA-F0-9]*) fail 'invalid SHA-256 checksum' ;; esac
printf '%s  %s\n' "$digest" "$asset" > "$scratch/checksum"
(cd "$scratch" && sha256sum -c checksum) || fail 'checksum verification failed'
# Extract only the expected regular executable; arbitrary archive paths are not
# written to disk. The package builder puts this file at the archive root.
tar -xzOf "$scratch/$asset" instmon > "$scratch/instmon"
[ -s "$scratch/instmon" ] || fail 'release archive contains no executable'
chmod 755 "$scratch/instmon"
"$scratch/instmon" --version

destination=${prefix%/}/bin
if [ -d "$destination" ] && [ -w "$destination" ]; then
    privileged=no
elif [ ! -e "$prefix" ] || [ -w "$prefix" ]; then
    mkdir -p "$destination" || fail "cannot create $destination"
else
    command -v sudo >/dev/null 2>&1 || fail 'use --prefix "$HOME/.local" for a user-only installation'
    privileged=yes
fi
run_install() {
    if [ "$privileged" = yes ]; then sudo "$@"; else "$@"; fi
}
run_install mkdir -p "$destination"
# A temporary file in the destination directory makes replacement atomic and
# allows upgrades even while an existing instmon process is running.
staged=$(run_install mktemp "$destination/.instmon.XXXXXXXX")
run_install install -m 755 "$scratch/instmon" "$staged"
run_install mv -f "$staged" "$destination/instmon"
staged=
echo "Installed $destination/instmon"
case ":$PATH:" in
    *":$destination:"*) echo 'Run: instmon' ;;
    *) printf 'Add this directory to PATH, then run instmon:\n  export PATH="%s:$PATH"\n' "$destination" ;;
esac
