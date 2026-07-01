# `Sentinel/` — ModusToolbox project root

> **For project overview, architecture, hardware target, and status**, see the
> [repository README](../README.md). This document is the build / flash /
> debug / OTA reference for the ModusToolbox project that lives here.

---

## Project layout

```
Sentinel/
├── src/
│   ├── app/                                # Main application
│   │   ├── main.cpp                        #   (built when TESTBENCH=0)
│   │   ├── cy_ota_config.h
│   │   └── GeneratedSource/                #   Device Configurator output
│   ├── testbench/                          # Testbench application
│   │   ├── testbench.cpp                   #   (built when TESTBENCH=1)
│   │   ├── cy_ota_config.h
│   │   └── GeneratedSource/
│   ├── bluetooth/                          # BLE stack glue + GATT handlers
│   │   ├── sentinel_ble_context.{hpp,cpp}  #   AIROC stack init, advertising, OTA
│   │   ├── sentinel_ble_gatt.{hpp,cpp}     #   GATT request dispatch
│   │   ├── sentinel_gatt_battery.hpp
│   │   ├── sentinel_gatt_debug.hpp
│   │   └── app_bt_utils.{h,c}              #   C-side helpers
│   ├── drivers/                            # Sensor / actuator drivers (C++)
│   │   ├── cyhal/                          #   Platform-specific concrete drivers
│   │   ├── flash-memory/                   #   W25Q128 driver
│   │   ├── led/
│   │   ├── rtc/                            #   DS3231 driver
│   │   ├── tachometer/
│   │   └── temperature-humidity-pressure/
│   │       ├── sentinel_bme280.{hpp,cpp}   #   Transport-agnostic C++ wrapper
│   │       └── bosch/                      #   Bosch BME280 vendor C driver
│   ├── transport/                          # Bus abstractions
│   │   ├── platform_agnostic/              #   CRTP façades (byte_transport, …)
│   │   └── cyhal/                          #   CYHAL-backed implementations
│   ├── task/                               # FreeRTOS tasks
│   │   ├── sentinel_task_battery_service.{hpp,cpp}
│   │   ├── sentinel_task_debug_stream.{hpp,cpp}
│   │   ├── sentinel_task_i2c_bus.{hpp,cpp}    # I²C bus arbiter
│   │   ├── sentinel_task_rtc_service.{hpp,cpp}
│   │   └── sentinel_task_spi_bus.{hpp,cpp}    # SPI bus arbiter
│   ├── test/                               # Smoke tests (exercised by testbench)
│   ├── logging/                            # Ring buffer + logi/logw/loge macros
│   ├── resource/                           # Inline cyhal_* peripheral handles
│   ├── utilities/                          # ring_buffer, span, endianess, …
│   └── driver_file_template/               # Boilerplate skeleton for new drivers
├── bsps/                                   # Board support packages
├── configs/
├── deps/
├── images/
├── keys/                                   # MCUboot signing keys
├── libs/                                   # MTB library cache (auto-managed)
├── scripts/                                # Build / program / vscode-sync scripts
├── third-party/
│   └── mcuboot/                            # MCUboot bootloader (git submodule)
├── vscode-json/                            # Canonical source for .vscode/{tasks,launch}.json
├── Makefile
├── flashmap.mk
├── openocd.tcl
├── setup-vscode.sh
└── README.md
```

### Two build configurations from one tree

One shared entry point (`src/main.cpp`) serves both builds; the Makefile's
`TESTBENCH` variable selects which per-target orchestrator compiles. The entry
point calls the common `sentinel::create_orchestrator()` symbol, defined once
per target and resolved at link time — no `#ifdef` in `main.cpp`:

| `TESTBENCH` | App name              | Orchestrator compiled                     | Excluded |
| :---------- | :-------------------- | :---------------------------------------- | :------- |
| `0` (default) | `sentinel-firmware`  | `src/app/sentinel_boot_orchestrator.*`    | `src/testbench/` is ignored |
| `1`         | `sentinel-testbench` | `src/testbench/sentinel_test_orchestrator.*` | `src/app/` is ignored |

`src/test/` is compiled in both configurations but only referenced from
`src/testbench/`. The build also defines `SENTINEL_APP=1` or
`SENTINEL_TESTBENCH=1` as a preprocessor symbol for conditional inclusion.

---

## Requirements

- [ModusToolbox™](https://www.infineon.com/modustoolbox) v3.0 or later (tested
  with v3.3, v3.5, and v3.8)
- Board support package (BSP) v5.0.0 or later
- C++17 toolchain
- MCUboot (vendored as a git submodule under `third-party/mcuboot/`)

### Supported toolchain

- GNU Arm® Embedded Compiler **v11.3.1** (`GCC_ARM`) — default

Other ModusToolbox-supported toolchains (Arm Compiler 6, IAR) have not been
tested against this project yet.

### Target kit

- **EZ-BLE Arduino Evaluation Board** ([`CYBLE-416045-EVAL`](https://www.infineon.com/cms/en/product/evaluation-boards/cyble-416045-eval/))

Other PSoC 6 Bluetooth LE kits should work with appropriate BSP and pin
changes, but only `CYBLE-416045-EVAL` is in active use.

> The Pioneer kits (`CY8CKIT-062-BLE`, `CY8CKIT-062-WIFI-BT`) ship with
> KitProg2. ModusToolbox requires KitProg3 — see the
> [Firmware Loader](https://github.com/Infineon/Firmware-loader) repo if you
> need to upgrade.

---

## Getting started

### Clone with submodules

MCUboot is vendored as a git submodule, so clone recursively:

```bash
git clone --recurse-submodules https://github.com/galudino/sentinel-firmware
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

### Configure the VS Code workspace

From the `Sentinel/` directory:

```bash
./setup-vscode.sh
```

This runs `make vscode` (a target provided by the ModusToolbox build system)
to generate the workspace file and copies the canonical `tasks.json` /
`launch.json` from `vscode-json/` into `.vscode/`. Open the generated
`*.code-workspace` in VS Code afterwards.

> Workspace files are gitignored — they're regenerated by `setup-vscode.sh`
> per clone.

---

## Building

### From VS Code

Use **Tasks: Run Task** (`Cmd/Ctrl+Shift+P` → *Tasks: Run Task*). The tasks
differentiate between the main application and the testbench:

| Task                                  | Output |
| :------------------------------------ | :----- |
| **Build** / **Build Debug**           | `sentinel-firmware` (Debug) |
| **Build Release**                     | `sentinel-firmware` (Release) |
| **Build Debug Testbench**             | `sentinel-testbench` (Debug) |
| **Build Release Testbench**           | `sentinel-testbench` (Release) |
| **Rebuild (Debug / Release / …)**     | Clean + Build for the selected variant |
| **Program Debug / Release**           | Build + flash the main application |
| **Program Debug / Release Testbench** | Build + flash the testbench application |
| **Build combined MCUBootApp and sentinel-firmware hex file (Debug / Release)** | MCUboot + app, single image |
| **Build combined MCUBootApp and sentinel-testbench hex file (Debug / Release)** | MCUboot + testbench, single image |
| **Clean** / **Erase Device**          | — |

The canonical source for these tasks lives in
[`vscode-json/tasks.json`](vscode-json/tasks.json) and
[`vscode-json/launch.json`](vscode-json/launch.json); they are copied into
`.vscode/` by `./scripts/update-vscode-json.sh`.

### From the command line

The `scripts/` directory has driver scripts that wrap the underlying `make`
invocations and handle the Python venv for image signing:

```bash
# Main application
./scripts/build-sentinel-firmware-debug.sh    nocopy
./scripts/build-sentinel-firmware-release.sh  nocopy

# Testbench application
./scripts/build-sentinel-testbench-debug.sh   nocopy
./scripts/build-sentinel-testbench-release.sh nocopy

# Program over KitProg3
./scripts/program-sentinel-firmware-debug.sh
./scripts/program-sentinel-testbench-release.sh
```

Pass `copy` instead of `nocopy` to also drop a timestamped `.bin` copy next
to the script invocation (useful for OTA payloads).

Or invoke `make` directly:

```bash
# Main app, Debug
make build CONFIG=Debug

# Testbench, Release
make build CONFIG=Release APPNAME=sentinel-testbench TESTBENCH=1

# Flash the most recently built image
make program CONFIG=Debug
```

---

## MCUboot bootloader

The CM0+ runs MCUboot; the CM4 runs `sentinel-firmware` (or
`sentinel-testbench`). The bootloader only needs to be programmed once per
board.

1. The bootloader and the application must agree on the flash map. Check this
   project's `Makefile` for the `OTA_FLASH_MAP` value; copy the matching
   flashmap from `<mtb_shared>/ota-update/release-vX.X.X/configs/` into
   `third-party/mcuboot/boot/cypress/`.

2. Build MCUboot:

    ```bash
    cd third-party/mcuboot/boot/cypress
    make clean app APP_NAME=MCUBootApp PLATFORM=PSOC_062_2M FLASH_MAP=./psoc62_2m_int_swap_single.json
    ```

   Verify `PLATFORM` and `FLASH_MAP` match your target.

3. Flash the resulting `MCUBootApp.hex` using
   [Cypress Programmer](https://softwaretools.infineon.com/tools/com.ifx.tb.tool.cypressprogrammer).
   After programming you should see the "no bootable image" banner on the UART
   terminal until you flash a signed Sentinel image on top.

---

## OTA firmware update

The application includes the Infineon OTA agent. To push an update:

1. Bump the version in the `Makefile` (`OTA_APP_VERSION_MAJOR` / `_MINOR` /
   `_BUILD`) and rebuild — **do not program**.
2. Locate the resulting signed `.bin` under `build/<TARGET>/<Config>/`.
3. Push it from a peer app such as
   [`WsOtaUpgrade.exe`](https://github.com/Infineon/btsdk-peer-apps-ota)
   (Windows):

    ```
    WsOtaUpgrade.exe sentinel-firmware.bin
    ```

4. The device receives the image into the secondary slot. On reboot, MCUboot
   validates and swaps it into the primary slot. If the new image fails to
   call `cy_ota_storage_validated()` within its runtime window, MCUboot
   reverts to the previously validated image on the next reboot.

To test the revert path, add `TEST_REVERT` to the `Makefile` `DEFINES` line,
build, push as an OTA, and observe MCUboot reverting after the bad image's
banner-and-reset sequence.

---

## Debugging

A `cortex-debug` launch configuration for each application variant lives in
[`vscode-json/launch.json`](vscode-json/launch.json):

- **Launch PSoC6 CM4 (KitProg3_MiniProg4) with sentinel-firmware**
- **Launch PSoC6 CM4 with sentinel-testbench (KitProg3_MiniProg4)**
- **Attach PSoC6 CM4 (KitProg3_MiniProg4) with sentinel-firmware**
- **Attach PSoC6 CM4 (KitProg3_MiniProg4) with sentinel-testbench**

Each launch config runs the appropriate **Build Debug** task as a
prerequisite. Open the Run and Debug panel (`Cmd/Ctrl+Shift+D`) and pick a
configuration; F5 starts a session.

> **CM4 caveat:** when stopping at `main()`, some early code may execute
> before the debugger halts. See
> [Infineon KBA231071](https://community.infineon.com/docs/DOC-21143) for the
> workaround.

---

## Logging

Two output paths are available simultaneously:

| Macro / call | Sink | When to use |
| :----------- | :--- | :---------- |
| `logi` / `logw` / `loge` / `logd` | BLE debug stream (ring buffer drained by a dedicated task and pushed as GATT notifications) | Step-by-step progress, sensor samples, anything a developer wants to see live from a paired client |
| `cy_log_msg` | retarget-IO UART (KitProg3 COM port, 115200 8N1) | PASS/FAIL summaries, init banners, anything that must survive ring-buffer overflow or absent BLE central |

> **Never** pass `%f` / `%e` / `%g` to `logi` / `logw` / `loge` / `logd`. The
> newlib `vsnprintf` float path uses 500–1500+ bytes of stack on ARM
> Cortex-M, which overflows the small task stacks used here. Scale to
> integers before formatting (e.g. `(int)(value * 100)` with a `%d.%02d`
> format).

---

## Origin & credits

This project began as a C++ rewrite of Infineon's
[`mtb-example-btstack-freertos-battery-server`](https://github.com/Infineon/mtb-example-btstack-freertos-battery-server)
and inherits its MCUboot + OTA agent integration. The Sentinel project has
diverged substantially — sensor stack, transport abstractions, persistent
storage, snapshot / event-log architecture — but is grateful for the
starting point.

All referenced product or service names and trademarks are property of their
respective owners. The Bluetooth® word mark and logos are registered
trademarks owned by Bluetooth SIG, Inc.
