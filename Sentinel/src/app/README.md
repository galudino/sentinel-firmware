# `app/` — production boot orchestrator

The firmware's entry stage. `main()` is deliberately thin: it hands off to
`app::boot_orchestrator`, a single **highest-priority FreeRTOS task** that brings
the whole system up in a fixed order and then deletes itself. Everything else in
the tree is started from here.

| | |
|---|---|
| **Key type** | `sentinel::app::boot_orchestrator` |
| **Files** | [`sentinel_boot_orchestrator.hpp`](sentinel_boot_orchestrator.hpp) · [`sentinel_boot_orchestrator.cpp`](sentinel_boot_orchestrator.cpp) |
| **Decisions** | [#13 boot orchestrator over a shared device context](../../../docs/architecture/decisions.md), [#17 post-scheduler Meyers singletons](../../../docs/architecture/decisions.md), [#18 `-fno-threadsafe-statics`](../../../docs/architecture/decisions.md) |

## What it does

The orchestrator runs once, in order:

1. Build the shared **device context** ([`resource::context()`](../resource/)) —
   the `bme` / `rtc` / `flash` drivers plus the event-log and snapshot stores.
2. `initialize_stores()` — scan both flash regions (fast-scan, #49).
3. Run [POST](../diagnostics/) against the **real** drivers, caching the first
   failure.
4. Start the event-log drain task (emits the boot sequence, then drains POST
   records).
5. Start the [service tasks](../task/) — rtc, bme280, snapshot-persistence,
   snapshot-stream, battery, cpu-die-temp.
6. `vTaskDelete(self)`.

A BLE-stack failure does **not** hard-assert — POST records it and boot proceeds
degraded (decision #12).

Because the orchestrator is the single task that first-touches every
function-local `static`, the singletons it builds are safe without guard
variables (`-fno-threadsafe-statics`, decision #18).

## How it's wired

`main.cpp` (in [`src/`](../)) obtains the orchestrator through
`sentinel::create_orchestrator()` — the shared entry symbol defined per target in
[`resource/`](../resource/), so one `main.cpp` serves both the firmware and
testbench builds. The testbench's twin is
[`test_orchestrator`](../testbench/).

```cpp
// main.cpp (shared by both targets)
int main(int, const char *[]) {
    const auto ble_stack_ok = sentinel::resource::system_initialize();  // BSP + clocks + banner

    // Creates the one-shot task (idle); its body runs once the scheduler
    // starts, so the bus arbiters can pump the I/O it issues (decision #13).
    auto result = sentinel::create_orchestrator(ble_stack_ok, ble_stack_ok);
    configASSERT(result == pdPASS);

    vTaskStartScheduler();   // never returns
    CY_ASSERT(false);
}
```

`create_orchestrator()` resolves to *this* target's orchestrator at link time
(the Makefile `CY_IGNORE`s the other target's dir), so the same `main.cpp` builds
both firmware and testbench (#51).

The full boot sequence diagram lives in the source-tree map:
[**Boot — the orchestrator**](../README.md#boot--the-orchestrator).

## See also

- [`resource/`](../resource/) — the device context + BSP bring-up it depends on.
- [`task/`](../task/) — the service tasks it starts.
- [`diagnostics/`](../diagnostics/) — POST + the System Event Log it drains.
- [`testbench/`](../testbench/) — the bottom-up test twin.
- [`../README.md`](../README.md) — source-tree map · [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md) — the "why".
