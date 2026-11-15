"""Hardware and optional-systemd checks independent of the test machine."""
import os
from pathlib import Path
import subprocess
import tempfile
import time

with tempfile.TemporaryDirectory(prefix='instmon-sysmon-') as temporary:
    root = Path(temporary)

    def write(name, content):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def probe(*args):
        result = subprocess.run(['./build/sysmon_probe', str(root), *args],
                                env=environment, capture_output=True, text=True, timeout=8)
        assert result.returncode == 0, result.stderr
        return result.stdout

    tools = root / 'bin'
    tools.mkdir()
    environment = dict(os.environ, PATH=f'{tools}:{os.environ["PATH"]}')
    output = probe()
    assert 'CPU temperature: unavailable' in output
    assert 'GPU: unavailable' in output
    assert 'systemd is not running' in output
    assert 'RAM: 6.00 / 8.00 GiB used (75.0%)' in output
    assert 'Uptime: 1 days 1 hours 1 minutes' in output

    write('etc/os-release', 'NAME=Ignored\nPRETTY_NAME="Test Linux\x1b[31m"\n')
    write('proc/cpuinfo', 'processor : 0\nmodel name : Test CPU\n')
    write('proc/loadavg', '1.25 2.50 3.75 1/50 99\n')
    # Two sockets reuse core 0; two sibling threads must count only once.
    for cpu, package in [(0, 0), (1, 0), (2, 1)]:
        write(f'sys/devices/system/cpu/cpu{cpu}/topology/physical_package_id', str(package))
        write(f'sys/devices/system/cpu/cpu{cpu}/topology/core_id', '0')
    write('sys/devices/system/cpu/cpu3/online', '0')
    write('sys/class/drm/card0/device/vendor', '0x8086\n')
    write('sys/class/drm/card0/device/device', '0x1234\n')
    write('sys/class/drm/card0-HDMI-A-1/device/vendor', '0x1002\n')
    write('sys/class/drm/card0-HDMI-A-1/device/device', '0xffff\n')
    write('sys/block/nvme0n1/size', '209715200\n')
    (root / 'sys/block/nvme0n1/device').mkdir()
    write('sys/class/hwmon/hwmon0/name', 'coretemp\n')
    sensor = write('sys/class/hwmon/hwmon0/temp1_input', '42000\n')
    write('sys/class/hwmon/hwmon1/name', 'amdgpu\n')
    write('sys/class/hwmon/hwmon1/temp1_input', '99000\n')
    output = probe()
    for expected in ['OS: Test Linux?[31m', 'CPU: Test CPU', '2 cores / 3 online threads',
                     'Intel (0x8086:0x1234)', 'nvme0n1 100.0 GiB', '42.0 C', '1.25 / 2.50 / 3.75']:
        assert expected in output, (expected, output)
    assert 'HDMI' not in output and '\x1b' not in output
    sensor.write_text('   \n')
    assert 'CPU temperature: unavailable' in probe()
    sensor.write_text('not a temperature\n')
    assert 'CPU temperature: unavailable' in probe()
    write('sys/class/thermal/thermal_zone0/type', 'x86_pkg_temp\n')
    write('sys/class/thermal/thermal_zone0/temp', '51000\n')
    assert '51.0 C' in probe()
    write('proc/loadavg', 'nan -2 3\n')
    assert 'Load: unavailable' in probe()

    # A mock systemctl exercises the real fork/pipe/timeout/cleanup code, without
    # assuming the host or CI container uses systemd as its service manager.
    (root / 'run/systemd/system').mkdir(parents=True)
    command = tools / 'systemctl'

    def service(body):
        command.write_text('#!/bin/sh\n' + body + '\n')
        command.chmod(0o755)

    service('[ "$*" = "--system --failed --type=service --no-legend --plain --no-pager" ] || exit 4; printf "broken.service loaded failed failed Example\\n"')
    assert 'broken.service' in probe()
    service('exit 0')
    assert 'cached up to 30s):\nnone' in probe()
    service('exit 1')
    assert 'restricted or manager unreachable' in probe()
    service('i=0; while [ "$i" -lt 500 ]; do printf "broken.service failed\\n"; i=$((i+1)); done')
    assert '[truncated service list]' in probe()
    command.unlink()
    environment['PATH'] = str(tools)
    assert 'systemctl missing' in probe()
    environment['PATH'] = f'{tools}:{os.environ["PATH"]}'
    service('exec sleep 10')
    start = time.monotonic()
    assert 'systemctl timed out' in probe()
    assert time.monotonic() - start < 5
    # Closing the UI during a query also kills and reaps the query promptly.
    start = time.monotonic()
    probe('early-shutdown')
    assert time.monotonic() - start < 1
print('Sysmon hardware, unavailable-data, caching and asynchronous-systemd tests passed')
