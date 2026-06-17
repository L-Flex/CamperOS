# CamperOS / CamperNode OS

ESP32-C6 Zigbee End Device firmware built with **ESP-IDF** (C / FreeRTOS), plus an
optional Home Assistant **ZHA quirk** (Python) under `integrations/homeassistant/zha/`.

## Cursor Cloud specific instructions

### Toolchain / environment
- This is an **ESP-IDF v5.4.4** project targeting **esp32c6**. The toolchain lives at
  `~/esp/esp-idf` and is installed by the startup update script. The startup script only
  installs the toolchain; system packages (`python3-venv`, `libslirp0`) and the QEMU
  emulator (`qemu-riscv32`) are baked into the VM snapshot, not reinstalled each run.
- **Every shell must source the ESP-IDF environment first** (it sets `IDF_PATH`, the
  compiler, and `idf.py` on `PATH`); it cannot be persisted by the update script:
  ```bash
  . ~/esp/esp-idf/export.sh
  ```

### Build / run (firmware)
- Standard ESP-IDF flow (see Espressif docs for full reference):
  ```bash
  idf.py set-target esp32c6   # first time; writes sdkconfig + fetches managed_components
  idf.py build                # fetches espressif/esp-zigbee-lib & esp-zboss-lib via component manager
  ```
- There is **no dev server** — "running" firmware means flashing to hardware
  (`idf.py -p <PORT> flash monitor`) or emulating in QEMU. No ESP32-C6 board is attached
  in this environment.
- **QEMU caveat:** the bundled `qemu-riscv32` only emulates the **esp32c3** machine, and
  esp32c6 additionally needs an 802.15.4 radio QEMU does not model — so the real firmware
  cannot be emulated here. To smoke-test the toolchain/run pipeline, build an ESP-IDF
  example for esp32c3 and run `idf.py qemu` (works end-to-end, prints app output).

### Known pre-existing source defects (NOT environment issues)
The firmware as committed does **not** compile under standard ESP-IDF C17. The ESP-IDF
framework and Zigbee managed components compile cleanly; failures are only in the app
sources, e.g.:
- `logger/src/logger.c` uses `bool` without `#include <stdbool.h>` (and `logger/include/logger.h` doesn't include it).
- `gpio/src/gpio_mgr.c` declares an ISR handler missing the IRAM attribute include (`error before 'gpio_isr_handler'`).
These are code bugs (missing includes), not toolchain problems; fix them in source to get a full `idf.py build`.

### Tests / lint
- No automated test suite and no CI in the repo. No configured C linter (`.clangd` is for
  editor diagnostics only).
- ZHA quirk (optional companion) is pure Python; validate by importing it with `zigpy`
  installed in a venv (`import campernode`). It registers manufacturer cluster `0xFC00`.
