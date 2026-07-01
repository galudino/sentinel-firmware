# Hardware bench setup

The current physical bench: board, buses, pinouts, and the wiring gotchas that
cost real debugging time. Rolled out of `docs/SESSION_HANDOFF.md` (2026-06-29).
Update when the bench wiring changes.

---

- **Board:** CYBLE-416045-EVAL, KitProg3 over USB (serial 115200 8N1).
- **I²C bus:** SCB6 @ 100 kHz — P6[5] SDA, P6[4] SCL. Devices: BME280 (0x76),
  DS3231 (0x68). DS3231 SQW on P6[3] (falling-edge 1 Hz → rtc_service).
- **SPI bus:** SCB1 @ 100 kHz — P10[0] MOSI (A5), P10[1] MISO (A4),
  P10[2] SCLK (A2), P10[3] SS0 (A3). Device: GD25Q128 flash on SS0.
  **W25Q128 HOLD# (pin 7) and WP# (pin 3) must be tied to VCC** — floating
  them was the cause of the "all 0x00 reads" debugging session.

> **SCB1, not SCB2, for SPI.** SCB2 hangs at boot on this BSP (decision #6 in
> [decisions.md](decisions.md)); SCB1 + P10[0..3] is the workaround.

## Local firmware build / flash

See [`Sentinel/README.md`](../../Sentinel/README.md) for the full build + flash
instructions. Quick reference:

```sh
CY_TOOLS_PATHS=/Applications/ModusToolbox/tools_3.8 make build TESTBENCH=1   # testbench app
CY_TOOLS_PATHS=/Applications/ModusToolbox/tools_3.8 make build TESTBENCH=0   # main app
```

- Both configs share one entry point, `src/main.cpp` (#51); `TESTBENCH` selects
  the per-target orchestrator it links. `TESTBENCH=1` compiles
  `src/testbench/` (test orchestrator); `TESTBENCH=0` compiles `src/app/` (boot
  orchestrator). The other directory is excluded via `CY_IGNORE`; all other
  `src/` sources (tasks, tests, drivers) compile into **both** builds.
- The `imgtool` `click` `ModuleNotFoundError` at the postbuild signing step is
  **benign** — the ELF + HEX are already produced; only the MCUBoot signing
  wrapper fails for lack of a Python `click` module. A clean link with that as
  the only `Error 1` means the build passed.
