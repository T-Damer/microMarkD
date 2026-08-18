#!/usr/bin/env python3

"""Build and run microMarkD in the CrossInk X4 Pro desktop simulator."""

from __future__ import annotations

import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
ENV = "micromarkd-x4pro-simulator"
CONFIG = ROOT / "platformio.simulator.ini"


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(2)


def main() -> int:
    system = platform.system()
    if system == "Windows":
        fail("native Windows is not supported; run this repository inside WSL")

    if shutil.which("pio") is None:
        fail("PlatformIO Core is missing; install it and ensure `pio` is on PATH")
    if shutil.which("sdl2-config") is None:
        if system == "Darwin":
            fail("SDL2 is missing; install it with `brew install sdl2`")
        fail("SDL2 is missing; on Debian/Ubuntu/WSL run `sudo apt install libsdl2-dev libssl-dev`")

    freeink = ROOT / "freeink-sdk" / "libs" / "ui" / "FreeInkUI"
    if not freeink.exists():
        fail("freeink-sdk submodule is missing; run `git submodule update --init --recursive`")

    # HalStorage maps the simulated SD root to ./fs_ by default. Creating the
    # vault up front makes it obvious where desktop notes live.
    (ROOT / "fs_" / "vault").mkdir(parents=True, exist_ok=True)

    command = [
        "pio",
        "run",
        "-c",
        str(CONFIG),
        "-e",
        ENV,
        "-t",
        "run_simulator",
    ]
    print("microMarkD X4 Pro simulator")
    print(f"SD root: {ROOT / 'fs_'}")
    print("Running:", " ".join(command))

    env = os.environ.copy()
    return subprocess.call(command, cwd=ROOT, env=env)


if __name__ == "__main__":
    raise SystemExit(main())
