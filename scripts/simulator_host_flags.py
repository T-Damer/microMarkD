#!/usr/bin/env python3

"""Emit host-specific flags for the native X4 Pro simulator build."""

from __future__ import annotations

import platform

system = platform.system()

if system == "Linux":
    # crossink-simulator uses OpenSSL for MD5 on Linux/WSL.
    print("-lssl -lcrypto -Wno-deprecated-declarations -Wno-narrowing")
elif system == "Darwin":
    # macOS uses CommonCrypto in the simulator shim.
    print("-Wno-c++11-narrowing")
else:
    # Native Windows is not supported by crossink-simulator. Keep config parsing
    # harmless so the launcher can emit the useful WSL guidance first.
    print("-Wno-c++11-narrowing")
