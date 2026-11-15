#!/bin/sh
# Called after a tested static build. Each architecture gets an independently
# verifiable archive, so the installer need not download other architectures.
set -eu
architecture=${1:?usage: scripts/package.sh x86_64|aarch64}
case "$architecture" in x86_64|aarch64) ;; *) echo 'Unsupported architecture' >&2; exit 1 ;; esac
mkdir -p dist
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
install -m 755 instmon "$scratch/instmon"
cp README.md THIRD_PARTY_NOTICES.md "$scratch/"
asset=instmon-linux-$architecture.tar.gz
tar -czf "dist/$asset" -C "$scratch" instmon README.md THIRD_PARTY_NOTICES.md
(cd dist && sha256sum "$asset" > "$asset.sha256")
