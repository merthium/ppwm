#!/usr/bin/env python3
"""Run only on a fresh nested display; always clean up our own child processes."""
import os
from pathlib import Path
import shutil
import select
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build'
for tool in ('Xephyr', 'xdotool'):
    if not shutil.which(tool):
        sys.exit(f'Missing tool: {tool}')
if not os.environ.get('DISPLAY'):
    sys.exit('Run from an X11 desktop terminal (DISPLAY is unset).')
processes = []
with tempfile.TemporaryDirectory(prefix='ppwm-test-') as folder:
    log_path = Path(folder) / 'xephyr.log'
    try:
        # Keep real desktop input out of synthetic pointer/focus tests.
        host = subprocess.Popen([str(build / 'xephyr_host')], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, text=True)
        processes.append(host)
        if not select.select([host.stdout], [], [], 5)[0]:
            raise RuntimeError('Timed out creating isolated test surface')
        parent_window = host.stdout.readline().strip()
        if not parent_window.isdigit():
            raise RuntimeError('Cannot create isolated test surface')
        # Let the X server choose an unused display; never reuse the host display.
        read_fd, write_fd = os.pipe()
        os.set_blocking(read_fd, False)
        with log_path.open('w') as log:
            server = subprocess.Popen(['Xephyr', '-parent', parent_window, '-displayfd', str(write_fd), '-screen',
                                       '1024x768', '-ac', '-nolisten', 'tcp'],
                                      pass_fds=(write_fd,), stdout=log, stderr=log)
            processes.append(server)
            os.close(write_fd)
            number = b''
            end = time.monotonic() + 10
            try:
                while time.monotonic() < end and server.poll() is None:
                    try:
                        number += os.read(read_fd, 64)
                    except BlockingIOError:
                        pass
                    if b'\n' in number:
                        break
                    time.sleep(.05)
            finally:
                os.close(read_fd)
            if not number.strip().isdigit():
                raise RuntimeError('Xephyr failed to start:\n' + log_path.read_text())
            env = dict(os.environ, DISPLAY=':' + number.strip().decode(),
                       PPWM_TEST_DISPLAY=':' + number.strip().decode())
            # Use a disposable executable so restart can exercise a replaced binary.
            executable = Path(folder) / 'ppwm'
            shutil.copy2(build / 'ppwm', executable)
            wm = subprocess.Popen([str(executable)], env=env, stdout=log, stderr=log)
            processes.append(wm)
            time.sleep(.5)
            if wm.poll() is not None:
                raise RuntimeError('ppwm exited:\n' + log_path.read_text())
            second = subprocess.run([str(build / 'ppwm')], env=env, capture_output=True,
                                    text=True, timeout=5)
            if second.returncode != 1 or 'another window manager' not in second.stderr:
                raise RuntimeError('Second WM ownership guard failed')
            if (build / 'cursor_test').exists():
                subprocess.run([str(build / 'cursor_test')], env=env, check=True, timeout=5)
            subprocess.run([str(build / 'x11_test')], env=env, check=True, timeout=30)
            subprocess.run([str(build / 'swallow_test')], env=env, check=True, timeout=30)
            subprocess.run([str(build / 'bar_test')], env=env, check=True, timeout=30)
            # Linux /proc: measure idle user+system CPU time, not an estimated percentage.
            def ticks():
                fields = Path(f'/proc/{wm.pid}/stat').read_text().split()
                return int(fields[13]) + int(fields[14])
            subprocess.run([str(build / "tray_test")], env=env, check=True, timeout=15)
            busy_before = ticks()
            subprocess.run([str(build / 'stress_test')], env=env, check=True, timeout=30)
            print(f'Stress WM CPU seconds: {(ticks() - busy_before) / os.sysconf("SC_CLK_TCK"):.3f}')
            before = ticks()
            time.sleep(3)
            cpu = (ticks() - before) / os.sysconf('SC_CLK_TCK')
            status = Path(f'/proc/{wm.pid}/status').read_text()
            rss = next(line for line in status.splitlines() if line.startswith('VmRSS:'))
            print(f'Idle CPU seconds / 3 wall seconds: {cpu:.3f}; {rss}')
            # Atomic replacement models a linker/install replacing the running inode.
            marker = Path(folder) / 'restarted'
            replacement = Path(folder) / 'replacement'
            replacement.write_text('#!/bin/sh\nprintf restarted > "' + str(marker) + '"\n')
            replacement.chmod(0o755)
            replacement.replace(executable)
            subprocess.run(['xdotool', 'key', '--clearmodifiers', 'alt+ctrl+shift+q'],
                           env=env, check=True, timeout=5)
            wm.wait(timeout=5)
            if wm.returncode != 0 or not marker.exists():
                raise RuntimeError('Restart did not execute the replacement binary')
            print('Restart executes replacement binary.')
            print('Nested integration and second-WM rejection passed.')
    except (RuntimeError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        print(log_path.read_text(), file=sys.stderr)
        sys.exit(1)
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
