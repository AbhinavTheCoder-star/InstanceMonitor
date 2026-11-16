"""Public TUI management tests. Only uniquely named disposable child processes
are ever confirmed as targets. Assert the exact target PID before every Enter.
"""
import errno
import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import time
import uuid

prefix = 'im-' + uuid.uuid4().hex[:7]
child_code = '''
import ctypes, signal, sys
ctypes.CDLL(None).prctl(15, sys.argv[1].encode(), 0, 0, 0)
if sys.argv[2] == 'ignore': signal.signal(signal.SIGTERM, signal.SIG_IGN)
print('ready', flush=True)
while True: signal.pause()
'''
children = []
master = slave = None
monitor = None


def spawn(suffix, mode='normal'):
    child = subprocess.Popen([sys.executable, '-c', child_code, prefix + suffix, mode],
                             stdout=subprocess.PIPE, text=True)
    children.append(child)
    assert child.stdout.readline().strip() == 'ready'
    return child


try:
    first = spawn('-a')
    second = spawn('-b', 'ignore')
    gone = spawn('-c')
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 180, 0, 0))
    before = termios.tcgetattr(slave)
    monitor = subprocess.Popen(['./instmon', '--filter', prefix, '--sort', 'pid', '--interval', '.25'],
                               stdin=slave, stdout=slave, stderr=slave,
                               env=dict(os.environ, TERM='xterm-256color'))

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

    query = prefix

    def search(child):
        global query
        # Clear the complete previous query, then search by numeric PID.
        send(b'/')
        send(b'\x7f' * len(query))
        query = str(child.pid)
        send(query.encode())
        send(b'\n')

    def request(child):
        prompt = send(b'k')
        assert f'Terminate PID {child.pid} ('.encode() in prompt, prompt
        assert (prefix.encode() in prompt), prompt

    pump(.6)
    # Arrow navigation, not row indices, determines the target. Refresh and sort
    # must retain that identity. Then reselect the intended test child explicitly.
    send(b'\x1bOB')
    request(second)
    send(b'\x1b')
    send(b'S')
    send(b'S')
    send(b'S')
    pump(.6)
    request(second)
    send(b'\x1b')
    search(first)
    request(first)
    send(b'\x1b')
    assert first.poll() is None
    # Resizing away the confirmation cancels it rather than accepting an unseen
    # destructive action. Restoring the terminal leaves normal navigation intact.
    request(first)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 3, 20, 0, 0))
    monitor.send_signal(signal.SIGWINCH)
    pump()
    send(b'\n')
    assert first.poll() is None
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 180, 0, 0))
    monitor.send_signal(signal.SIGWINCH)
    pump()
    send(b'\x1b')
    # Escape cancels selection too; Enter and k must not act on a hidden row.
    send(b'\x1b')
    send(b'\n')
    assert b'No current process selected' in send(b'k')
    assert first.poll() is None
    search(first)
    request(first)
    # No signal before confirmation, even across several refreshes. Other keys
    # inside the prompt cannot change its frozen target or open another view.
    for keys in [b'\x1bOB', b'S', b'/', b'k', b's']:
        send(keys)
    pump(.6)
    assert first.poll() is None and second.poll() is None
    assert b'SIGTERM sent' in send(b'\n')
    assert first.wait(timeout=5) == -signal.SIGTERM
    assert second.poll() is None
    # A target exiting during confirmation reports failure, never retargeting.
    search(gone)
    request(gone)
    gone.terminate()
    gone.wait(timeout=5)
    pump(.6)
    assert b'already exited' in send(b'\n')
    assert second.poll() is None
    # SIGTERM may be ignored: Instmon must neither promise exit nor escalate.
    search(second)
    request(second)
    assert b'SIGTERM sent' in send(b'\n')
    pump(1)
    assert second.poll() is None
    # While searching, k is literal text and cannot invoke a termination prompt.
    send(b'/')
    assert b'Terminate PID' not in send(b'k')
    send(b'\x1b')
    send(b'q')
    assert monitor.wait(timeout=5) == 0
    assert termios.tcgetattr(slave) == before
finally:
    if monitor and monitor.poll() is None:
        monitor.kill()
        monitor.wait()
    for child in children:
        if child.poll() is None:
            child.kill()  # Test cleanup only; the application never escalates.
        child.wait()
        child.stdout.close()
    if master is not None:
        os.close(master)
        os.close(slave)
print('Management navigation, PID search, cancellation, frozen targets and SIGTERM tests passed')
