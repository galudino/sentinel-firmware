# `sentinel-firmware`

BLE-connected embedded telemetry and diagnostics platform built on PSoC 6, ModusToolbox, and FreeRTOS — with persistent logging, real-time `device_snapshot` telemetry, and OTA-ready architecture.

> ⚠ **Work in progress.** APIs, services, file layout, and persistent record formats are all subject to change. The project began as a C++ rewrite of Infineon's [`mtb-example-btstack-freertos-battery-server`](https://github.com/Infineon/mtb-example-btstack-freertos-battery-server) and is steadily diverging from it; references to the battery-server demo have been removed from this document, but artifacts (e.g. the battery service task, some configurator outputs) may still be visible in source while the migration completes.

---

## Sensors

Phase I:
- [I2C] BME280 — temperature, humidity, pressure (ambient)
- [I2C] DS3231 — RTC for timestamps
- [SPI] W25Q128 — external flash for persistent storage

Phase II:
- [I2C] TMP117 — fan-load temperature
- [I2C] INA-219 — fan current / power monitor
- [PWM] NF-A4x10 — 5 V PWM fan (with tachometer feedback)
- [SPI] Motor?

Phase III:
- [SPI] Display?

## Primary functions

Phase I:

- **Environment readings**
  - The BME280 reports ambient temperature, humidity, and pressure.
- **OTA firmware update (DFU)**
  - MCUboot bootloader on the CM0+ core; the updatable application runs on the CM4 core.
  - Firmware update files are stored on the cloud, downloaded to the client application device, and transmitted via BLE.
- **Persistent data storage** on the W25Q128:
  - Serial number (user-configurable)
  - Firmware version
  - System Event Log (collection of `system_event_record`)
  - Recent snapshot history (collection of periodic `device_snapshot`)
  - System preferences
- **System event log** (`system_event_record[]`)
  - Discrete events, state transitions, warnings, diagnostics.
  - Examples (not definitive):
    - `firmware_update_event`
    - `configuration_change_event`
    - `thermal_event`
    - `power_event`
    - `connectivity_event`
  - Entire system event log can be captured over a BLE characteristic
- **Live Telemetry**
  - Capture live 'device snapshots' -- instantaneous device state/sensor readings to client application (via BLE)
- **Telemetry History**
  - Periodic device snapshots are stored in flash at a user-defined interval.
  - Entire device snapshot history can be captured over a BLE characteristic.

Phase II:
- **Thermally-controlled fan**
  - Firmware drives the NF-A4x10 PWM fan.
    - Minimum RPM is user-configurable.
    - If fan-load temperature rises above a configured threshold, RPM is increased automatically.
  - Fan-load temperature is measured by the TMP117.
  - Fan current and power are measured by the INA-219.
- Motor?

- Phase III:
  - Display for sensor values
  - Cycle through on-screen values with a button

## Bluetooth LE services & characteristics (WIP, not definitive)

### Phase I
- **System**
  - [read/write] Serial Number
  - [read] Firmware Version
  - [write] Request bootloader mode (for OTA DFU)
- **Debug**
  - [read/notify] Output Stream
  - [read/write] Output Stream Notify Enable
- **OTA FW Upgrade Service**
  - [write/notify/indicate] OTA Upgrade Control Point
  - [write] OTA Upgrade Data 
- **Sensor**
  - [read/notify] Ambient Temperature, Humidity, Pressure
  - [read/write] Unix Time
  - [read/notify] RTC operating temperature
- **System Event Log**
  - [read] Record Count
  - [read/write] Log Index
  - [read] Record Block
  - [read/write] Clear System Event Log Store
- **Live Telemetry**
  - [read/notify] Current Device Snapshot
  - [read/write] Snapshot Notify Enable
- **Telemetry History**
  - [read] History Record Count
  - [read/write] History Index
  - [read] History Record Block
  - [read/write] Clear History Store 

### Phase II
- **Fan/Thermal Load**
    - [read] Current Fan RPM
    - [read/write] User-defined min/max fan RPM
    - [read] Fan Voltage
    - [read] Fan Power
    - [read] TMP117 temperature reading
    - [read/write] User-defined max TMP117 temperature threshold
    - [read] Current Motor RPM
    - [read/write] User-defined min/max motor RPM
    - [read] Motor Voltage
    - [read] Motor Power

---

## WIP System Event Record definition

Still iterating on the best way to represent this. The basic shape is a 36-byte tagged record, where `event_type` selects how the trailing `data[28]` is interpreted.

```cpp
struct system_event_record {
    uint32_t unix_time;

    uint8_t _padding0[3];

    enum event_type : uint8_t {
        firmware_update_attempted,
        firmware_update_successful,
        firmware_update_failed,
        fan_rpm_threshold_change,
        fan_rpm_min_threshold_met,
        fan_rpm_max_threshold_exceeded,
        fan_temperature_threshold_change,
        fan_temperature_min_threshold_met,
        fan_temperature_max_threshold_exceeded,
        fan_power_consumption_exceeded,
        ble_stack_initialized,
        ble_stack_deinitialized,
        ble_peripheral_connected,
        ble_peripheral_disconnected
    } event_type;

    uint8_t data[28];
}; // 36 bytes

struct firmware_update_event_record {
    uint32_t unix_time;          // 0  + 4  = 4
    uint8_t  _padding0[3];       // 4  + 3  = 7
    uint8_t  event_type;         // 7  + 2  = 9
    firmware_version version;    // 9  + 24 = 32
    uint8_t  _padding1[4];       // 32 + 4  = 36
}; // 36 bytes
```

> If a `firmware_update_event_record` of type `firmware_update_attempted` is immediately followed by one of type `firmware_update_successful`, comparing the `version` fields tells you the start/end versions of the upgrade.

```cpp
struct fan_rpm_threshold_change_record {
    uint32_t unix_time;
    uint8_t  _padding0[3];
    uint8_t  event_type;
    uint16_t old_min_fan_rpm;
    uint16_t new_min_fan_rpm;
    uint16_t old_max_fan_rpm;
    uint16_t new_max_fan_rpm;
    uint8_t  _padding2[20];
}; // 36 bytes

struct fan_rpm_threshold_record {
    uint32_t unix_time;
    uint8_t  _padding0[3];
    uint8_t  event_type;
    uint16_t current_fan_rpm;
    uint16_t fan_threshold_min_rpm;
    uint16_t fan_threshold_max_rpm;
    uint8_t  _padding1[22];
}; // 36 bytes

// Likewise: fan_temperature_threshold_change_record,
//           fan_temperature_threshold_record,
//           fan_power_consumption_record, etc.
```

## WIP Device Snapshot definition

A `device_snapshot` represents the instantaneous state of the system, timestamped via the RTC. Fields and ordering may still change; this is the current shape.

```cpp
struct device_snapshot {
    firmware_version fw_version;
    uint32_t serial_number;

    uint32_t unix_time;

    uint16_t ambient_temperature;
    uint16_t ambient_humidity;
    uint16_t ambient_pressure;

    uint16_t fan_threshold_min_rpm;
    uint16_t fan_threshold_max_rpm;
    uint16_t fan_current_rpm;
    uint16_t fan_voltage;
    uint16_t fan_power;
    uint16_t fan_temperature_threshold;
    uint16_t fan_temperature;
};
```

---

## Project structure

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
│   │   ├── sentinel_ble_context.{hpp,cpp}  #   stack init, advertising, OTA
│   │   ├── sentinel_ble_gatt.{hpp,cpp}     #   GATT request dispatch
│   │   ├── sentinel_gatt_battery.hpp
│   │   ├── sentinel_gatt_debug.hpp
│   │   └── app_bt_utils.{h,c}              #   C-side helpers
│   ├── drivers/                            # Sensor / actuator drivers (C++)
│   │   ├── cyhal/                          #   Platform-specific concrete drivers
│   │   ├── led/
│   │   ├── tachometer/
│   │   └── temperature-humidity-pressure/
│   │       ├── sentinel_bme280.{hpp,cpp}   #   Transport-agnostic C++ wrapper
│   │       └── bosch/                      #   Bosch BME280 vendor C driver
│   ├── transport/                          # Bus abstractions
│   │   ├── platform_agnostic/              #   CRTP façades (byte_transport, …)
│   │   └── cyhal/                          #   CYHAL-backed implementations
│   ├── task/                               # FreeRTOS tasks
│   │   ├── sentinel_task_battery_service.{hpp,cpp}
│   │   └── sentinel_task_debug_stream.{hpp,cpp}
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

The Makefile uses a `TESTBENCH` variable to swap between two top-level entry points without two separate projects:

| `TESTBENCH` | App name              | Entry point             | Excluded |
| :---------- | :-------------------- | :---------------------- | :------- |
| `0` (default) | `sentinel-firmware`  | `src/app/main.cpp`      | `src/testbench/` is ignored |
| `1`         | `sentinel-testbench` | `src/testbench/testbench.cpp` | `src/app/` is ignored |

`src/test/` is compiled in both configurations but only referenced from `src/testbench/`. The build also defines `SENTINEL_APP=1` or `SENTINEL_TESTBENCH=1` as a preprocessor symbol for conditional inclusion.

---

## Requirements

- [ModusToolbox™](https://www.infineon.com/modustoolbox) v3.0 or later (tested with v3.3 and v3.5)
- Board support package (BSP) v5.0.0 or later
- C++17 toolchain
- MCUboot (vendored as a git submodule under `third-party/mcuboot/`)

### Supported toolchain

- GNU Arm® Embedded Compiler **v11.3.1** (`GCC_ARM`) — default

Other ModusToolbox-supported toolchains (Arm Compiler 6, IAR) have not been tested against this project yet.

### Target kit

- **EZ-BLE Arduino Evaluation Board** ([`CYBLE-416045-EVAL`](https://www.infineon.com/cms/en/product/evaluation-boards/cyble-416045-eval/))

Other PSoC 6 Bluetooth LE kits should work with appropriate BSP and pin changes, but only `CYBLE-416045-EVAL` is in active use.

> The Pioneer kits (`CY8CKIT-062-BLE`, `CY8CKIT-062-WIFI-BT`) ship with KitProg2. ModusToolbox requires KitProg3 — see the [Firmware Loader](https://github.com/Infineon/Firmware-loader) repo if you need to upgrade.

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

This runs `make vscode` (a target provided by the ModusToolbox build system) to generate the workspace file and copies the canonical `tasks.json` / `launch.json` from `vscode-json/` into `.vscode/`. Open the generated `*.code-workspace` in VS Code afterwards.

> Workspace files are gitignored — they're regenerated by `setup-vscode.sh` per clone.

---

## Building

### From VS Code

Use **Tasks: Run Task** (`Cmd/Ctrl+Shift+P` → *Tasks: Run Task*). The tasks differentiate between the main application and the testbench:

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

The canonical source for these tasks lives in [`vscode-json/tasks.json`](vscode-json/tasks.json) and [`vscode-json/launch.json`](vscode-json/launch.json); they are copied into `.vscode/` by `./scripts/update-vscode-json.sh`.

### From the command line

The `scripts/` directory has driver scripts that wrap the underlying `make` invocations and handle the Python venv for image signing:

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

Pass `copy` instead of `nocopy` to also drop a timestamped `.bin` copy next to the script invocation (useful for OTA payloads).

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

The CM0+ runs MCUboot; the CM4 runs `sentinel-firmware` (or `sentinel-testbench`). The bootloader only needs to be programmed once per board.

1. The bootloader and the application must agree on the flash map. Check this project's `Makefile` for the `OTA_FLASH_MAP` value; copy the matching flashmap from `<mtb_shared>/ota-update/release-vX.X.X/configs/` into `third-party/mcuboot/boot/cypress/`.

2. Build MCUboot:

    ```bash
    cd third-party/mcuboot/boot/cypress
    make clean app APP_NAME=MCUBootApp PLATFORM=PSOC_062_2M FLASH_MAP=./psoc62_2m_int_swap_single.json
    ```

   Verify `PLATFORM` and `FLASH_MAP` match your target.

3. Flash the resulting `MCUBootApp.hex` using [Cypress Programmer](https://softwaretools.infineon.com/tools/com.ifx.tb.tool.cypressprogrammer). After programming you should see the "no bootable image" banner on the UART terminal until you flash a signed Sentinel image on top.

---

## OTA firmware update

The application includes the Infineon OTA agent. To push an update:

1. Bump the version in the `Makefile` (`OTA_APP_VERSION_MAJOR` / `_MINOR` / `_BUILD`) and rebuild — **do not program**.
2. Locate the resulting signed `.bin` under `build/<TARGET>/<Config>/`.
3. Push it from a peer app such as [`WsOtaUpgrade.exe`](https://github.com/Infineon/btsdk-peer-apps-ota) (Windows):

    ```
    WsOtaUpgrade.exe sentinel-firmware.bin
    ```

4. The device receives the image into the secondary slot. On reboot, MCUboot validates and swaps it into the primary slot. If the new image fails to call `cy_ota_storage_validated()` within its runtime window, MCUboot reverts to the previously validated image on the next reboot.

To test the revert path, add `TEST_REVERT` to the `Makefile` `DEFINES` line, build, push as an OTA, and observe MCUboot reverting after the bad image's banner-and-reset sequence.

---

## Debugging

A `cortex-debug` launch configuration for each application variant lives in [`vscode-json/launch.json`](vscode-json/launch.json):

- **Launch PSoC6 CM4 (KitProg3_MiniProg4) with sentinel-firmware**
- **Launch PSoC6 CM4 with sentinel-testbench (KitProg3_MiniProg4)**
- **Attach PSoC6 CM4 (KitProg3_MiniProg4) with sentinel-firmware**
- **Attach PSoC6 CM4 (KitProg3_MiniProg4) with sentinel-testbench**

Each launch config runs the appropriate **Build Debug** task as a prerequisite. Open the Run and Debug panel (`Cmd/Ctrl+Shift+D`) and pick a configuration; F5 starts a session.

> **CM4 caveat:** when stopping at `main()`, some early code may execute before the debugger halts. See [Infineon KBA231071](https://community.infineon.com/docs/DOC-21143) for the workaround.

---

## Logging

Two output paths are available simultaneously:

| Macro / call | Sink | When to use |
| :----------- | :--- | :---------- |
| `logi` / `logw` / `loge` / `logd` | BLE debug stream (ring buffer drained by a dedicated task and pushed as GATT notifications) | Step-by-step progress, sensor samples, anything a developer wants to see live from SentinelPanel |
| `cy_log_msg` | retarget-IO UART (KitProg3 COM port, 115200 8N1) | PASS/FAIL summaries, init banners, anything that must survive ring-buffer overflow or absent BLE central |

> **Never** pass `%f` / `%e` / `%g` to `logi` / `logw` / `loge` / `logd`. The newlib `vsnprintf` float path uses 500–1500+ bytes of stack on ARM Cortex-M, which overflows the small task stacks used here. Scale to integers before formatting (e.g. `(int)(value * 100)` with a `%d.%02d` format).

---

## Origin & credits

This project began as a C++ rewrite of Infineon's [`mtb-example-btstack-freertos-battery-server`](https://github.com/Infineon/mtb-example-btstack-freertos-battery-server) and inherits its MCUboot + OTA agent integration. The Sentinel project has diverged substantially — sensor stack, transport abstractions, persistent storage, snapshot/event-log architecture — but is grateful for the starting point.

All referenced product or service names and trademarks are property of their respective owners. The Bluetooth® word mark and logos are registered trademarks owned by Bluetooth SIG, Inc.
