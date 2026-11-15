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
             ('--sort', 'bogus'), ('inspect', '1', '--filter', 'x'), ('--sysmon',),
             ('--sysmon', '--json'), ('list', '--sysmon'), ('inspect', '1', '--sysmon')]:
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

# Exercise the public TUI in a real terminal and assert visible view labels,
# rather than merely sending keys and checking that the process did not crash.
def terminal_session(arguments, starts_in_sysmon):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 35, 150, 0, 0))
    before = termios.tcgetattr(slave)
    environment = dict(os.environ, TERM='xterm-256color')
    process = subprocess.Popen(['./instmon', '--interval', '.25', *arguments],
                               stdin=slave, stdout=slave, stderr=slave, env=environment)

    def pump(seconds=.2):
        output = bytearray()
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([master], [], [], .02)[0]:
                try:
                    output.extend(os.read(master, 65536))
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
        return bytes(output)

    def send(keys):
        os.write(master, keys)
        return pump()

    try:
        first = pump(.6)
        if starts_in_sysmon:
            assert b'System Monitor (Sysmon)' in first and b'System Information' in first
            assert b'CPU temperature:' in first and b'Failed systemd services' in first
            assert b'Filter:' in send(b'm')
        else:
            assert b'Filter:' in first and b'System Information' not in first
        # Sort cycling is still available at the agreed Shift+S shortcut.
        assert b'CPU' in send(b'S')
        assert b'System Monitor (Sysmon)' in send(b's')
        send(b'j')
        send(b'k')
        assert b'Filter:' in send(b'm')
        # Search accepts navigation letters as literal input, without switching.
        send(b'/')
        search = send(b'sm')
        assert b'System Monitor (Sysmon)' not in search
        send(b'\x1b')
        assert b'Overview' in send(b'\n')
        assert b'Status / threads' in send(b'\t')
        # Tab panel cycling, detail navigation and Sysmon do not break selection.
        send(b'j')
        assert b'System Monitor (Sysmon)' in send(b's')
        assert b'Filter:' in send(b'm')
        send(b'?')
        send(b'?')
        send(b's')
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 5, 12, 0, 0))
        process.send_signal(signal.SIGWINCH)
        pump()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 35, 150, 0, 0))
        process.send_signal(signal.SIGWINCH)
        pump()
        send(b'm')
        if starts_in_sysmon:
            process.send_signal(signal.SIGTERM)
        else:
            send(b'q')
        pump()
        assert process.wait(timeout=5) == 0
        assert termios.tcgetattr(slave) == before
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


terminal_session([], False)
terminal_session(['--sysmon'], True)
print('CLI, JSON, both TUI views, navigation and terminal cleanup tests passed')
