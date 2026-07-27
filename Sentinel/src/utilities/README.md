# `utilities/` — header-only helpers

Small, dependency-light building blocks used across the tree. All header-only.

| Header | Provides |
|---|---|
| [`sentinel_span.hpp`](sentinel_span.hpp) | `span<T>` — a non-owning view (pointer + size); the byte-buffer currency of the drivers/transports |
| [`sentinel_ring_buffer.hpp`](sentinel_ring_buffer.hpp) | `ring_buffer` — fixed-capacity circular buffer (backs the debug stream) |
| [`sentinel_endianess.hpp`](sentinel_endianess.hpp) | `endianess` enum + byte-order load/store helpers for multi-byte wire fields |
| [`sentinel_firmware_version.hpp`](sentinel_firmware_version.hpp) | `firmware_version` — compile-time version (`major.minor.patch.build`) |
| [`sentinel_platform_id.hpp`](sentinel_platform_id.hpp) | `platform_id` / `vendor_id` — machine-stable identity for the GATT/cloud wire |
| [`sentinel_build_time.hpp`](sentinel_build_time.hpp) | Compile-time build-timestamp parsers + an RTC-sync helper (seed the clock from `__DATE__`/`__TIME__`) |
| [`sentinel_utilities.hpp`](sentinel_utilities.hpp) | Misc: `unused(...)` (const-ref variadic), `to_underlying(...)`, etc. |

```cpp
uint8_t buf[4];
sentinel::span<uint8_t> s(buf, sizeof buf);        // non-owning view
constexpr auto v = sentinel::current_firmware_version;  // compile-time version
// v.c_str() -> "major.minor.patch.build"
```

> `firmware_version::c_str()` formats `major.minor.patch.build`; use `array()[3]`
> for the full 16-bit build (`build()` truncates to 8 bits — noted at the call
> site).

## See also

- Used by nearly every module — drivers/transports (`span`), [`logging/`](../logging/) (`ring_buffer`), [`bluetooth/`](../bluetooth/) (`firmware_version`, `platform_id`).
- [`../README.md`](../README.md) — source-tree map.
