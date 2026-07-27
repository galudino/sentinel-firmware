# `testbench/` — bottom-up test orchestrator

The testbench target's entry stage — the **twin of the [boot
orchestrator](../app/)**. Where the boot orchestrator brings up production
services, `test_orchestrator` brings up the buses and runs every [`test/`](../test/)
suite bottom-up, then tallies the result over serial.

| | |
|---|---|
| **Key type** | `sentinel::test_orchestrator` |
| **Files** | [`sentinel_test_orchestrator.cpp`](sentinel_test_orchestrator.cpp) · [`sentinel_test_orchestrator.hpp`](sentinel_test_orchestrator.hpp) |
| **Issue** | #48 |

## What it does

Selected at link time by the shared [`create_orchestrator()`](../resource/) symbol
when the build is `TESTBENCH=1` (the Makefile `CY_IGNORE`s [`app/`](../app/)), so
the same [`main.cpp`](../) boots it. It owns the bus transports as fixtures,
builds the shared context once up front (so the reader suites don't race to
first-construct it), runs each suite's `run_all()` to completion, and prints a
pass/fail tally.

```
… post bme280 PASS … record_store 7/7 … system_event_log 8/8 …  47/47
```

## Build & flash

Use the release testbench for a size-accurate, OTA-signing build:

```bash
CY_TOOLS_PATHS=/Applications/ModusToolbox/tools_3.8 \
  ./scripts/build-sentinel-testbench-release.sh nocopy
```

See [`docs/architecture/hardware-bench.md`](../../../docs/architecture/hardware-bench.md)
for the full build/flash quick reference.

## See also

- [`test/`](../test/) — the suites it runs · [`app/`](../app/) — the production twin.
- [`resource/`](../resource/) — the shared context + entry symbol.
- [`../README.md`](../README.md) — source-tree map.
