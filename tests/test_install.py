"""Installer contract tests: fake downloads, real checksum/extraction/install.

All destinations live under TemporaryDirectory. No sudo, network, or global
installation is used. A bad download must leave an existing executable intact.
"""
import hashlib
import io
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='instmon-install-test-') as directory:
    root = Path(directory)
    tools = root / 'tools'
    tools.mkdir()
    payload = root / 'release.tar.gz'
    binary = b'#!/bin/sh\necho "instmon 0.1.0"\n'
    with tarfile.open(payload, 'w:gz') as archive:
        info = tarfile.TarInfo('instmon')
        info.size = len(binary)
        info.mode = 0o755
        archive.addfile(info, io.BytesIO(binary))
    checksum = root / 'digest'
    checksum.write_text(hashlib.sha256(payload.read_bytes()).hexdigest() + '  ignored-name\n')
    (tools / 'uname').write_text('#!/bin/sh\ncase "$1" in -s) echo Linux;; -m) echo "$TEST_ARCH";; esac\n')
    # Mimic curl's output-file and latest-redirect behavior without fetching URLs.
    (tools / 'curl').write_text('''#!/bin/sh
url= destination=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --output) destination=$2; shift 2 ;;
        https:*) url=$1; shift ;;
        *) shift ;;
    esac
done
case "$url" in
    */releases/latest) printf 'https://github.com/AbhinavTheCoder-star/InstanceMonitor/releases/tag/v0.1.0' ;;
    *.sha256) cp "$TEST_DIGEST" "$destination" ;;
    *.tar.gz) cp "$TEST_ARCHIVE" "$destination" ;;
    *) exit 1 ;;
esac
''')
    for tool in tools.iterdir():
        tool.chmod(0o755)
    env = dict(os.environ, PATH=f'{tools}:{os.environ["PATH"]}', TEST_ARCH='x86_64',
               TEST_DIGEST=str(checksum), TEST_ARCHIVE=str(payload))
    prefix = root / 'path with spaces'

    def run(*args, success=True):
        result = subprocess.run(['sh', str(ROOT / 'install.sh'), '--prefix', str(prefix), *args],
                                env=env, capture_output=True, text=True, timeout=10)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result

    run()
    executable = prefix / 'bin/instmon'
    assert executable.read_bytes() == binary
    assert os.access(executable, os.X_OK)
    env['TEST_ARCH'] = 'aarch64'
    run('--version', 'v0.1.0')
    original = executable.read_bytes()
    checksum.write_text('0' * 64 + '  ignored\n')
    run(success=False)
    assert executable.read_bytes() == original
    checksum.write_text('invalid digest\n')
    run(success=False)
    env['TEST_ARCH'] = 'riscv64'
    run(success=False)
    env['TEST_ARCH'] = 'x86_64'
    run('--version', '../bad', success=False)
    run('--version', success=False)
    run('--prefix', 'relative', success=False)
    assert not list((prefix / 'bin').glob('.instmon.*'))
print('Installer tests passed (x86-64, ARM64, checksums, upgrades, invalid arguments)')
