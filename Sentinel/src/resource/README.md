# `resource/` — shared state & system bring-up

Process-wide state and hardware bring-up: the **shared device context** (drivers
+ flash stores as one object), the BSP/system initialization, and the per-target
orchestrator entry symbol.

| | |
|---|---|
| **Key entry points** | `resource::context()`, `resource::system_initialize()`, `sentinel::create_orchestrator()` |
| **Files** | [`sentinel_device_context.hpp`](sentinel_device_context.hpp) · [`sentinel_resource.hpp`](sentinel_resource.hpp) / [`.cpp`](sentinel_resource.cpp) · [`sentinel_orchestrator_entry.hpp`](sentinel_orchestrator_entry.hpp) |
| **Decisions** | [#13 shared device context](../../../docs/architecture/decisions.md), [#17 post-scheduler Meyers singleton](../../../docs/architecture/decisions.md) |

## Device context — `resource::context()`

A Meyers singleton (`device_context`) that owns the shared drivers and stores so
every task borrows the *same* instance rather than constructing its own:

```cpp
using bme280_t   = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>;
using ds3231_t   = sentinel::ds3231  <sentinel::cyhal_i2c_bus_transport>;
using w25q128_t  = sentinel::w25q128 <sentinel::cyhal_spi_bus_transport>;
using event_log_t = sentinel::diagnostics::system_event_log<event_store_t>;
```

It is **first constructed inside the boot orchestrator**, post-scheduler — which
is why the singleton is safe without guard variables (decision #18). `rtc_service`
and `bme280_service` borrow `ctx.rtc` / `ctx.bme`; `initialize_stores()` scans
both flash regions and binds the event log.

```cpp
auto &ctx = sentinel::resource::context();   // build-once, borrow-many
ctx.initialize_stores();                     // scan flash, bind event log
```

## System bring-up — `system_initialize()`

Hoisted out of the two near-identical target `main` bodies: clocks, BSP,
peripherals, the serial banner (`APP_NAME_STRING`), and BLE-stack init. Returns
whether the BLE stack came up (feeds POST; a failure degrades boot rather than
asserting).

## Orchestrator entry — `create_orchestrator()`

One declared symbol, defined per target in that target's orchestrator TU
([`app/`](../app/) → boot orchestrator, [`testbench/`](../testbench/) → test
orchestrator). The Makefile `CY_IGNORE`s the other dir so the linker resolves it
with no `#ifdef` — this is what lets one [`main.cpp`](../) serve both builds (#51).

## See also

- [`app/`](../app/) — the boot orchestrator that builds the context.
- [`drivers/`](../drivers/) · [`storage/`](../storage/) · [`diagnostics/`](../diagnostics/) — the parts the context aggregates.
- [`../README.md`](../README.md) — source-tree map · [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md).
