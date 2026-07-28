# `sentinel-firmware`

BLE-connected embedded telemetry and diagnostics platform built on PSoC 6,
ModusToolbox, and FreeRTOS — with persistent logging, real-time
`device_snapshot` telemetry, and BLE OTA firmware update over MCUBoot.

> **Status: Phase I complete — public `v1.0.0`.** The Phase I MVP on the
> Infineon CYBLE-416045 is feature-complete and validated on-bench, including
> end-to-end OTA DFU. The project began as a C++ rewrite of Infineon's
> [`mtb-example-btstack-freertos-battery-server`](https://github.com/Infineon/mtb-example-btstack-freertos-battery-server)
> template and has since diverged substantially. Phases II–IV (driver
> expansion, then Raspberry Pi 5 / POSIX and Nordic nRF5340 ports) are future
> work — see the
> [milestones](https://github.com/galudino/sentinel-firmware/milestones) and
> [project board](https://github.com/users/galudino/projects/2). API reference
> (Doxygen, auto-deployed): **https://galudino.github.io/sentinel-firmware/**.

---

## What this is

A platform-agnostic embedded firmware that:

- **Reads ambient environment** (temperature, humidity, pressure) from a BME280.
- **Keeps accurate time** with a DS3231 RTC and a 1 Hz square-wave interrupt for
  drift-free synchronization.
- **Persists state to flash** (W25Q128 SPI NOR) — a system event log and a
  rolling history of device-state snapshots, both retrievable later.
- **Streams live telemetry over BLE** — paired clients can subscribe to live
  sensor readings, fetch the event log, replay snapshot history, or capture
  one-off live snapshots during a session.
- **Supports OTA firmware update** over BLE via MCUBoot.
- **Targets multiple platforms** — Infineon PSoC 6 BLE today (Phase I),
  Raspberry Pi 5 (POSIX / Linux) in Phase III, and Nordic nRF5340 (Zephyr /
  nRF Connect SDK) in Phase IV, all behind the same platform-agnostic abstractions.

The companion is the **`sentinel-client`** SwiftUI application (separate repo).
The firmware is designed to pair with it without client-side assumptions about
which platform served the data.

---

## Status

| Phase | Scope | Progress |
|---|---|---|
| **I — MVP on Infineon CYBLE-416045** | Environmental sensing, event log, snapshot history, live snapshot stream, OTA DFU | ✅ **Complete — `v1.0.0`** · 33 / 33 issues closed · OTA validated on-bench |
| **II — Driver expansion + refinements** | Full driver backlog (chips spanning I²C / SPI / GPIO / PWM / 1-Wire), BME280 SPI mode, CI, refinements to Phase I | Backlog |
| **III — Raspberry Pi 5 (POSIX) port** | Port the platform-agnostic layer to Raspberry Pi 5 (POSIX / Linux) | Planned |
| **IV — Nordic nRF5340 port** | Port to Nordic nRF5340 (Zephyr / nRF Connect SDK) | Planned |

See [**Project board**](https://github.com/users/galudino/projects/2) for
detailed status, [**Milestones**](https://github.com/galudino/sentinel-firmware/milestones)
for phase-level progress, the [**API reference**](https://galudino.github.io/sentinel-firmware/)
for generated docs, and [**docs/SESSION_HANDOFF.md**](docs/SESSION_HANDOFF.md)
for current architectural context.

---

## Hardware target (Phase I)

| Subsystem | Part | Bus / interface | Issue |
|---|---|---|---|
| MCU | PSoC 6 BLE — CY8C6347BZI-BLD53 (CYBLE-416045-02 module) | — | [#24](https://github.com/galudino/sentinel-firmware/issues/24) |
| Dev kit | CYBLE-416045-EVAL | — | — |
| Environmental sensor | Bosch BME280 | I²C @ 0x76 | [#14](https://github.com/galudino/sentinel-firmware/issues/14) |
| Real-time clock | Maxim/Analog DS3231 | I²C @ 0x68 + SQW GPIO on P6[3] | [#15](https://github.com/galudino/sentinel-firmware/issues/15), [#5](https://github.com/galudino/sentinel-firmware/issues/5) |
| External flash | Winbond W25Q128JV (+ known-good clones from GigaDevice / XTX / Boya / ZBIT) | SPI on SCB1 SS0 | [#1](https://github.com/galudino/sentinel-firmware/issues/1) |
| Bluetooth LE | AIROC stack on the integrated CM0+ core | — | [#29](https://github.com/galudino/sentinel-firmware/issues/29) |
| Debug UART | KitProg3 USB bridge | SCB5 UART, 115200 8N1 | — |

---

## Architecture at a glance

```
┌────────────────────────────────────────────────────────────────┐
│                       Application layer                        │
│                                                                │
│   ┌────────────┐  ┌────────────┐  ┌──────────────┐  ┌────────┐ │
│   │  Sample    │  │  Snapshot  │  │  System      │  │  POST  │ │
│   │  tasks     │  │  service   │  │  event log   │  │        │ │
│   └─────┬──────┘  └──────┬─────┘  └──────┬───────┘  └────────┘ │
│         │                │               │                     │
│         │     (consume drivers + record_store + BLE)            │
└─────────┼────────────────┼───────────────┼─────────────────────┘
          │                │               │
┌─────────┼────────────────┼───────────────┼─────────────────────┐
│         │  Drivers  /  Transport CRTP  / Bus arbiters          │
│         ▼                ▼               ▼                     │
│   ┌────────────┐  ┌────────────┐  ┌──────────────┐             │
│   │ sentinel:: │  │ sentinel:: │  │ sentinel::   │             │
│   │  bme280    │  │  ds3231    │  │  w25q128     │             │
│   └─────┬──────┘  └──────┬─────┘  └──────┬───────┘             │
│         │                │               │                     │
│         │ byte_transport<_, i2c_tag>     │ byte_transport       │
│         │                │               │ <_, spi_tag>         │
│         ▼                ▼               ▼                     │
│   ┌────────────────────────────┐  ┌──────────────────────┐     │
│   │  I²C bus arbiter (task)    │  │ SPI bus arbiter (task)│     │
│   └─────────────┬──────────────┘  └────────────┬──────────┘     │
└─────────────────┼─────────────────────────────┼────────────────┘
                  ▼                             ▼
┌─────────────────────────────────────────────────────────────────┐
│       Platform-specific transport implementation               │
│                                                                │
│   CYHAL today (Phase I) — Zephyr / NCS in Phase IV for nRF5340  │
│   — POSIX in Phase III for Raspberry Pi 5                       │
└─────────────────────────────────────────────────────────────────┘
```

**Key design choice:** drivers and bus arbiters are written against a generic
`byte_transport<Derived, BusTag>` CRTP base. The concrete implementation
(`cyhal_*_transport`, future `linux_*_transport`, future `zephyr_*_transport`)
is what changes per platform. Driver code is unchanged across platforms.

---

## Quick build (Phase I, Infineon target)

Requires ModusToolbox 3.8+ and the CYBLE-416045-EVAL board attached. Build from
`Sentinel/` with the wrapper scripts — they set up the imgtool signing venv and
re-apply the vendored `ota-update` patch (see the OTA note below):

```sh
cd Sentinel
export CY_TOOLS_PATHS=/Applications/ModusToolbox/tools_3.8   # adjust to your install

# Release firmware — BLE/OTA require the Release config; OTA .bin lands in build/
./scripts/build-sentinel-firmware-release.sh nocopy

# Testbench — every driver validated against real hardware, with a serial tally
./scripts/build-sentinel-testbench-release.sh nocopy

# MCUBoot + firmware, combined into one .hex (flash via ModusToolbox Programmer)
./scripts/build-mcuboot-app-release.sh nocopy
./scripts/combine-mcuboot-app-and-sentinel-firmware-release.sh
```

Open a serial terminal at `/dev/tty.usbmodem*` (macOS) / `/dev/ttyACM*`
(Linux) / `COMn` (Windows), 115200 8N1, to see boot logs and test output.

> **OTA note:** `mtb_shared/` is gitignored, so a fix to Infineon's `ota-update`
> v2.0.0 flash writer (a row-boundary bug that corrupts OTA images) is carried by
> `scripts/apply-mtb-shared-patches.sh` and applied automatically by the build
> scripts. Build via the scripts — not a bare `make build` — so OTA images are
> written correctly.

Full build details in [`Sentinel/README.md`](Sentinel/README.md).

---

## Repository layout

```
sentinel-firmware/
├── README.md                       # ← you are here
├── LICENSE
├── docs/
│   ├── SESSION_HANDOFF.md          # rolling context for new contributors
│   └── architecture/               # durable reference (decisions, infra, bench, layout)
├── mtb_shared/                     # ModusToolbox dependencies (gitignored)
└── Sentinel/                       # ModusToolbox project root
    ├── README.md                   # Build / flash / project layout details
    ├── src/                        # All C++ source (see src/README.md — module map)
    ├── scripts/                    # build / program / combine / patch helpers
    ├── patches/                    # local fixes to gitignored mtb_shared libs
    ├── bsps/                       # Board support
    ├── configs/                    # FreeRTOS + MCUBoot + signing config
    └── third-party/                # MCUBoot (submodule)
```

The choice to nest the ModusToolbox project inside `Sentinel/` rather than at
the repository root is a convention that keeps the build system's concerns
separated from project-wide concerns (docs, license, README). Trade-off: the
GitHub-displayed README at the repo root and the build instructions are in
different files. A future repository restructure may flatten this.

---

## Tech stack

- **Language:** modern C++17, RAII-first, CRTP for compile-time polymorphism in
  driver / transport hot paths.
- **RTOS:** FreeRTOS 10.x (CM4 core).
- **HAL:** Infineon CYHAL (Phase I); POSIX + libgpiod (Phase III, Pi 5);
  Zephyr drivers (Phase IV, nRF5340).
- **BLE stack:** AIROC (Cypress / Infineon) Bluetooth LE stack (Phase I);
  BlueZ (Phase III, Pi 5); Zephyr Bluetooth subsystem (Phase IV, nRF5340).
- **Bootloader / OTA:** MCUBoot signed images, OTA delivery over BLE.
- **Toolchain:** ModusToolbox 3.8 + GCC `arm-none-eabi` 14.2 (MCUBootApp builds
  with 11.3); CMake for the Phase III Linux port.
- **Persistence:** raw W25Q128 SPI NOR; no filesystem; circular record store
  for fixed-size records.

---

## Project rules

- **Public.** This repository is public and portfolio-quality (Phase I is
  closed). Contributions, issues, and external references stay scoped to
  sentinel itself.
- **Branch workflow.** git-flow style: branch off `develop`, incremental
  commits, tag the pre-squash tip, squash-merge into `develop`, merge `develop`
  into `main` with an explicit `--no-ff` merge commit. See
  [`docs/architecture/github-infrastructure.md`](docs/architecture/github-infrastructure.md)
  for the full convention.
- **Issue body templates.** "Completed" for closed retros; forward-spec for
  backlog items. Templates documented in
  [`docs/architecture/github-infrastructure.md`](docs/architecture/github-infrastructure.md).

---

## Author

Gemuele Aludino — [github.com/galudino](https://github.com/galudino)

## License

See [LICENSE](LICENSE).
