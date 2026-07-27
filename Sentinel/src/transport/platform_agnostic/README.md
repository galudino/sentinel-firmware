# `transport/platform_agnostic/` — the CRTP interfaces

The portable half of [`transport/`](../): CRTP façades that define *what* an I/O
peripheral can do, with **no** platform code. Concrete implementations (e.g.
[`cyhal/`](../cyhal/)) derive from these and supply the HAL-backed behavior.

| Interface | File | What it abstracts |
|---|---|---|
| `byte_transport<Impl, Tag>` | [`sentinel_byte_transport.hpp`](sentinel_byte_transport.hpp) | I²C / SPI byte transfers (tag-specialized: `i2c_tag`, `spi_tag`) |
| `serial_port<Impl>` | [`sentinel_serial_port.hpp`](sentinel_serial_port.hpp) | UART: config (`parity_mode`, `stop_bits_mode`, `flow_control_mode`), read/write |
| `pwm_signal<Impl>` | [`sentinel_pwm_signal.hpp`](sentinel_pwm_signal.hpp) | PWM duty/frequency control |
| `timer<Impl>` | [`sentinel_timer.hpp`](sentinel_timer.hpp) | Timer/counter start/stop/read |
| `gpio_line` / `delay_api` | [`sentinel_gpio_line.hpp`](sentinel_gpio_line.hpp) | Digital output line + delay hooks |
| `spi_flash_bus_controller<T>` | [`sentinel_spi_flash_bus_controller.hpp`](sentinel_spi_flash_bus_controller.hpp) | CS/DC/RESET pin choreography for SPI flash (`spi_flash_pins`) |

## Why CRTP (not virtual)

Static polymorphism: the base forwards to the derived implementation through a
`static_cast`, so calls inline and there is **zero vtable / runtime cost** on the
MCU. The trade-off — the concrete type is fixed at compile time — is exactly what
firmware wants.

```cpp
template <typename Implementation>
class byte_transport<Implementation, i2c_tag> {
    auto write(const uint8_t *tx, size_t n, uint32_t timeout_ms = 0,
               bool send_stop = true) noexcept {
        return impl().write(tx, n, timeout_ms, send_stop);   // forwards to derived
    }
    // ... write_read (repeated-start), read, delay_us, spans ...
};
```

`byte_transport` is split into an `i2c_tag` specialization (addressing, STOP,
repeated-start) and an `spi_tag` specialization (full-duplex transfers) so each
protocol exposes only the operations that apply to it — no SFINAE, no ambiguous
method set.

## See also

- [`cyhal/`](../cyhal/) — the PSoC 6 implementations of these interfaces.
- [`../README.md`](../README.md) — transport overview · [`../../README.md`](../../README.md) — source-tree map.
- [`docs/architecture/decisions.md`](../../../../docs/architecture/decisions.md) — #26 (CRTP base).
