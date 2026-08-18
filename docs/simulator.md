# microMarkD X4 Pro desktop simulator

microMarkD can run natively on macOS or Linux/WSL through
[`uxjulia/crossink-simulator`](https://github.com/uxjulia/crossink-simulator).
The simulator renders the Xteink X4 Pro 800x480 framebuffer in an SDL2 window
and maps host input to the X4 Pro controls.

The simulator dependency is pinned in `platformio.simulator.ini` so a fresh
checkout uses the same revision that CI validates.

## 1. Clone with submodules

```bash
git clone --recursive https://github.com/T-Damer/microMarkD.git
cd microMarkD
git checkout agent/micromarkd-bootstrap
```

For an existing clone:

```bash
git checkout agent/micromarkd-bootstrap
git pull
git submodule update --init --recursive
```

## 2. Install prerequisites

### macOS

```bash
brew install sdl2
```

Install PlatformIO Core if `pio --version` does not work. For example:

```bash
python3 -m pip install -U platformio
```

### Debian / Ubuntu / WSL

```bash
sudo apt update
sudo apt install libsdl2-dev libssl-dev
python3 -m pip install -U platformio
```

Native Windows is not supported by the upstream simulator. Use WSL.

## 3. Run

Recommended launcher:

```bash
python3 scripts/run_x4pro_simulator.py
```

Equivalent direct PlatformIO command:

```bash
pio run -c platformio.simulator.ini \
  -e micromarkd-x4pro-simulator \
  -t run_simulator
```

After the binary has been built once, it can also be launched without a rebuild:

```bash
pio run -c platformio.simulator.ini \
  -e micromarkd-x4pro-simulator \
  -t run_simulator_no_build
```

## Simulated SD card

By default the simulator maps:

```text
./fs_/        -> SD card root /
./fs_/vault/  -> /vault/
```

The launcher creates `./fs_/vault/` automatically. Put ordinary Obsidian-style
Markdown files there while the simulator is stopped, for example:

```text
fs_/vault/
  Home.md
  Medicine/
    Diabetes.md
```

You can choose another simulated SD root without changing the project:

```bash
CROSSPOINT_SIM_SD=/absolute/path/to/sd \
  python3 scripts/run_x4pro_simulator.py
```

## Controls

| Input | X4 Pro action |
| --- | --- |
| Mouse | touch, tap, drag, swipe |
| Up / Down | page back / forward side buttons |
| Left / Right | left / right front buttons |
| Return | confirm / select |
| Escape | back |
| H | capacitive Home key |
| P | power |
| S | simulated sleep |

## Automated input and screenshots

The upstream simulator can drive deterministic UI tests. Example:

```bash
mkdir -p qa-artifacts
CROSSPOINT_SIM_INPUT_SCRIPT='1500:TAP:180,120;3000:HOME:100;4000:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='2300:./qa-artifacts/micromarkd.bmp' \
  .pio/build/micromarkd-x4pro-simulator/program
```

Touch coordinates use the displayed logical 800x480 framebuffer.

## What this does not emulate

This is a firmware/UI simulator, not a cycle-accurate ESP32-S3/X4 Pro emulator.
It is useful for navigation, rendering, touch hitboxes, vault I/O, editor/search
flows, networking shims, and deterministic screenshots. It does not faithfully
model physical e-ink waveform timing/ghosting, ESP32 memory pressure, real GT911
or SDMMC timing, PSRAM behaviour, or sudden-power-loss characteristics.
