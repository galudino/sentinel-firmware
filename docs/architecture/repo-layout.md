# Where things live in the repo

Map of the source tree. Rolled out of `docs/SESSION_HANDOFF.md` (2026-06-29).

---

```
sentinel-firmware/
├── README.md                       # Project overview (start here)
├── docs/
│   ├── SESSION_HANDOFF.md          # Rolling status: what just merged, what's next
│   └── architecture/               # Durable reference (this directory)
│       ├── decisions.md            # Cumulative architectural decisions (#1–#16…)
│       ├── github-infrastructure.md # Board/field/option IDs, gh recipes, contrib rules
│       ├── hardware-bench.md       # Bus pinouts, bench wiring, local build
│       └── repo-layout.md          # ← you are here
├── mtb_shared/                     # ModusToolbox shared deps (gitignored)
└── Sentinel/                       # ModusToolbox project root
    ├── README.md                   # Build / flash instructions
    ├── src/
    │   ├── app/                    # main.cpp + Device Configurator output
    │   ├── testbench/              # alternate entry for testbench builds
    │   ├── bluetooth/              # AIROC BLE stack wrappers + GATT
    │   ├── drivers/                # Device drivers (CRTP-templated)
    │   ├── transport/
    │   │   ├── platform_agnostic/  # CRTP byte_transport base + tags
    │   │   └── cyhal/              # CYHAL-specific concrete transports
    │   ├── task/                   # FreeRTOS tasks (bus arbiters, services)
    │   ├── telemetry/              # device_snapshot + populate()
    │   ├── diagnostics/            # POST + System Event Log
    │   ├── storage/                # record_store (flash-backed circular)
    │   ├── test/                   # Testbench test suites
    │   ├── resource/               # Peripheral resource singletons
    │   ├── logging/                # Ring buffer + debug stream helpers
    │   └── utilities/              # Small headers (span, ring_buffer, etc.)
    ├── bsps/                       # ModusToolbox board support packages
    ├── configs/                    # FreeRTOS + MCUBoot + signing config
    └── third-party/                # MCUBoot
```
