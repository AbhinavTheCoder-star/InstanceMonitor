"""Exercise the executable through its public CLI and a real pseudo-terminal."""
import ctypes
import errno
import fcntl
import json
import os
import pty
import select
import signal
import struct
import subprocess
import termios
import time


def run(*args, code=0):
    result = subprocess.run(['./instmon', *args], capture_output=True, text=True, timeout=15)
    assert result.returncode == code, (args, result.returncode, result.stderr)
    return result.stdout


assert 'Usage:' in run('--help')
for args in [('inspect', '-1'), ('--interval', 'nan'), ('--interval', '1'),
             ('--sort', 'bogus'), ('inspect', '1', '--filter', 'x')]:
    run(*args, code=2)
run('inspect', '2147483647', code=1)
data = json.loads(run('list', '--json'))
assert data['schema_version'] == 1 and data['memory_total_bytes'] > 0
assert any(p['pid'] == os.getpid() for p in data['processes'])
assert all(p['cpu_percent'] is None for p in data['processes'])
assert json.loads(run('list', '--user', '4294967294', '--json'))['processes'] == []
# Set a process name containing JSON metacharacters, then round-trip it.
assert ctypes.CDLL(None).prctl(15, b'name"\\test', 0, 0, 0) == 0
inspection = json.loads(run('inspect', str(os.getpid()), '--json'))['processes'][0]
assert inspection['name'] == 'name"\\test'
assert inspection['details']['identity_valid']
assert inspection['rss_bytes'] > 0 and 'executable' in inspection['details']
assert 'Descriptors' in run('inspect', str(os.getpid()))

# Verify an actual curses session can search, inspect, switch panels, resize,
# return, quit, and restore terminal attributes. Drain output to avoid blocking.
master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 120, 0, 0))
before = termios.tcgetattr(slave)
environment = dict(os.environ, TERM='xterm-256color')
process = subprocess.Popen(['./instmon', '--interval', '.25'], stdin=slave, stdout=slave, stderr=slave, env=environment)


def pump(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if select.select([master], [], [], .02)[0]:
            try:
                os.read(master, 65536)
            except OSError as error:
                if error.errno != errno.EIO:
                    raise


try:
    pump(.4)
    for keys in [b'/', b'python', b'\n', b'\n', b'\t', b'j', b'\x1b', b'?']:
        os.write(master, keys)
        pump(.12)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 5, 12, 0, 0))
    process.send_signal(signal.SIGWINCH)
    pump(.2)
    os.write(master, b'q')
    pump(.2)
    assert process.wait(timeout=5) == 0
    assert termios.tcgetattr(slave) == before
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
    os.close(master)
    os.close(slave)
print('CLI, JSON and terminal integration tests passed')
