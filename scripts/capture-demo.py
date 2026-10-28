#!/usr/bin/env python3
"""Build and capture real demo clients in a private Xephyr; never use the host WM."""
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]


def main():
    if not os.environ.get("DISPLAY"):
        raise RuntimeError("Run from an X11 desktop (DISPLAY is unset).")
    for tool in ("Xephyr", "c++", "cmake", "ctest"):
        if not shutil.which(tool):
            raise RuntimeError(f"Missing tool: {tool}")
    capture = shutil.which("import") or shutil.which("maim")
    if not capture:
        raise RuntimeError("Install ImageMagick (import) or maim to capture the screen.")

    processes = []
    with tempfile.TemporaryDirectory(prefix="ppwm-demo-") as folder:
        temporary = Path(folder)
        output = []
        # The output displayed in the demo is produced by these actual commands.
        commands = [
            ["cmake", "--build", "build", "--target", "ppwm", "layout_test", "-j2"],
            ["ctest", "--test-dir", "build", "--output-on-failure", "-R", "^layout$"],
        ]
        for command in commands:
            result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, check=True, timeout=120)
            output.extend(["$ " + " ".join(command), result.stdout.strip(), ""])
        transcript = temporary / "build.txt"
        transcript.write_text("\n".join(output).replace(str(ROOT), "."))
        demo = temporary / "demo"
        subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Wpedantic",
                        str(ROOT / "tests/demo.cpp"), "-lX11", "-o", str(demo)],
                       check=True, timeout=60)
        log_path = temporary / "session.log"
        try:
            with log_path.open("w") as log:
                read_fd, write_fd = os.pipe()
                try:
                    server = subprocess.Popen(
                        ["Xephyr", "-displayfd", str(write_fd), "-screen", "1280x800",
                         "-ac", "-nolisten", "tcp"], pass_fds=(write_fd,), stdout=log, stderr=log)
                    processes.append(server)
                finally:
                    os.close(write_fd)
                try:
                    if not select.select([read_fd], [], [], 10)[0]:
                        raise RuntimeError("Timed out starting Xephyr")
                    number = os.read(read_fd, 64).strip()
                finally:
                    os.close(read_fd)
                if not number.isdigit():
                    raise RuntimeError("Xephyr did not supply a display number")
                display = ":" + number.decode("ascii")
                if display == os.environ["DISPLAY"]:
                    raise RuntimeError("Refusing to use the host display")
                env = dict(os.environ, DISPLAY=display, PPWM_DEMO_DISPLAY=display)
                wm = subprocess.Popen([str(ROOT / "build/ppwm")], env=env, stdout=log, stderr=log)
                processes.append(wm)
                time.sleep(0.5)
                if wm.poll() is not None:
                    raise RuntimeError("ppwm exited before demo launch")
                client = subprocess.Popen([str(demo), str(ROOT / "src/layout.hpp"), str(transcript)],
                                          env=env, stdout=log, stderr=log)
                processes.append(client)
                time.sleep(3)
                if any(process.poll() is not None for process in processes):
                    raise RuntimeError("A demo process exited before capture")
                destination = ROOT / "assets/ppwm.png"
                destination.parent.mkdir(exist_ok=True)
                command = ([capture, "-window", "root", str(destination)]
                           if Path(capture).name == "import" else [capture, str(destination)])
                subprocess.run(command, env=env, check=True, timeout=15)
                print(destination)
        except Exception:
            print(log_path.read_text())
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == "__main__":
    main()
