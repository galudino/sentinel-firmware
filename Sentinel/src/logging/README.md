# `logging/` — unified logging facade

One logging call routes to every configured sink — the serial port *and* the BLE
debug stream — so firmware code logs once and doesn't care where the bytes land.

| | |
|---|---|
| **Key API** | `logi(...)` / `loge(...)`, `logging::log`, `logging::level`, `logging::sink::*` |
| **Files** | [`sentinel_log.hpp`](sentinel_log.hpp) / [`.cpp`](sentinel_log.cpp) · [`sentinel_log_sink.hpp`](sentinel_log_sink.hpp) · [`sentinel_format_string.hpp`](sentinel_format_string.hpp) / [`.cpp`](sentinel_format_string.cpp) · [`sentinel_debug_print.hpp`](sentinel_debug_print.hpp) / [`.cpp`](sentinel_debug_print.cpp) |

## Facade + sinks

`logging::level` is the severity enum; `logging::sink::serial` and
`logging::sink::ble_debug` are the portable backends. The `logi` / `loge` macros
(info / error) are the everyday entry points:

```cpp
logi("boot: post %s in %lu ms", passed ? "PASS" : "fail", ms);
loge("bme280: last_error=%d", sensor.last_error());
```

- `sentinel_format_string.*` — the lightweight `printf`-style formatter (no libc
  `printf` pulled onto the MCU).
- `sentinel_debug_print.*` — the BLE debug-stream **ring buffer** + raw print that
  backs the `ble_debug` sink; the [`debug_stream`](../task/) task drains it to the
  GATT debug characteristic.

> The `logi` / `loge` macros use `,##__VA_ARGS__` (a GNU extension) — fine on
> `arm-none-eabi-gcc`; when host-compiling add
> `-Wno-gnu-zero-variadic-macro-arguments`.

A portable logging facade unifying these behind one call is tracked in #50.

## See also

- [`task/`](../task/) — `debug_stream` (drains the ring buffer to BLE).
- [`bluetooth/`](../bluetooth/) — `gatt::debug` (the debug characteristic).
- [`utilities/`](../utilities/) — `ring_buffer` primitive · [`../README.md`](../README.md) — source-tree map.
