# `task/` — FreeRTOS tasks (OO / class style)

Every long-running FreeRTOS task lives here, and every one is a **class**
(decision #16, standardized by #47): the task owns its state as private members
and exposes a Meyers-singleton `instance()` + `task_create()`. No task keeps
mutable state in file-static globals.

| | |
|---|---|
| **Bus arbiters** | `i2c_bus`, `spi_bus` — one task per physical bus (request/response queue) |
| **Service tasks** | `rtc_service`, `bme280_service`, `cpu_die_temp_service`, `battery_service`, `debug_stream` |
| **Snapshot lanes** | `snapshot_persistence_task` (lane 1, flash), `snapshot_stream_task` (lane 2, BLE) |
| **Maintenance** | `ble_maintenance_task` — async BLE-triggered clear-store / bootloader request |
| **Decisions** | [#16 tasks are OO/class](../../../docs/architecture/decisions.md), [#27 / #28 bus arbiters](../../../docs/architecture/decisions.md), [#14 two-lane snapshot](../../../docs/architecture/decisions.md) |

## The pattern

```cpp
class bme280_service {
public:
    static bme280_service &instance() noexcept;   // single instance
    BaseType_t task_create() noexcept;            // starts the task
    sample latest() const noexcept;               // published accessor
private:
    static void task_trampoline(void *);          // -> run()
    void run();                                    // the loop
    sample            m_latest{};                  // state = private members
    SemaphoreHandle_t m_latest_mutex{nullptr};
    TaskHandle_t      m_handle{nullptr};
};
```

Started from the [boot orchestrator](../app/); consumers read published accessors:

```cpp
sentinel::task::rtc_service::instance().task_create();
const auto t = sentinel::task::rtc_service::instance().last_unix_time();
```

ISRs (battery HAL timer, RTC SQW edge) are private `static` members that recover
the instance from the HAL callback argument — so no file-static task state remains.

## Bus arbiters

A single peripheral is shared by several driver consumers on different tasks. Each
bus is owned by **one arbiter task** (`i2c_bus` / `spi_bus`); consumers enqueue an
`i2c_request` / `spi_request` (target, tx/rx, a response queue) and block on their
own response queue. See [**Bus arbiter task model**](../README.md#bus-arbiter-task-model).

## Two snapshot lanes

`snapshot_persistence_task` writes periodic snapshots to flash (~5 min);
`snapshot_stream_task` streams live snapshots over BLE only while a subscriber is
enabled (~100 ms, idle otherwise). See [**Two-lane snapshot model**](../README.md#two-lane-snapshot-model).

## See also

- [`drivers/`](../drivers/) — what the service tasks drive · [`transport/`](../transport/) — the bus transports the arbiters own.
- [`telemetry/`](../telemetry/) · [`storage/`](../storage/) — snapshot production + persistence.
- [`../README.md`](../README.md) — source-tree map · [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md).
