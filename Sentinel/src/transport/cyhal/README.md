# `transport/cyhal/` — PSoC 6 (CYHAL) implementations

The platform half of [`transport/`](../): concrete transports that implement the
[`platform_agnostic/`](../platform_agnostic/) CRTP interfaces on top of Infineon's
**CYHAL** (Cypress Hardware Abstraction Layer). Swap these out for another
platform's adapters and the drivers above are unchanged.

| Implementation | File | Implements |
|---|---|---|
| `cyhal_i2c_bus_transport` | [`sentinel_cyhal_i2c_bus_transport.hpp`](sentinel_cyhal_i2c_bus_transport.hpp) | `byte_transport<…, i2c_tag>` **via the [`i2c_bus`](../../task/) arbiter task** |
| `cyhal_spi_bus_transport` | [`sentinel_cyhal_spi_bus_transport.hpp`](sentinel_cyhal_spi_bus_transport.hpp) | `byte_transport<…, spi_tag>` **via the [`spi_bus`](../../task/) arbiter task** |
| `cyhal_i2c_transport` | [`sentinel_cyhal_i2c_transport.hpp`](sentinel_cyhal_i2c_transport.hpp) | `byte_transport<…, i2c_tag>` (direct, inline HAL) |
| `cyhal_spi_transport` | [`sentinel_cyhal_spi_transport.hpp`](sentinel_cyhal_spi_transport.hpp) | `byte_transport<…, spi_tag>` (direct, inline HAL) |
| `cyhal_uart_port<…>` | [`sentinel_cyhal_uart_port.hpp`](sentinel_cyhal_uart_port.hpp) | `serial_port` (RX ring buffer, templated capacity) |
| `cyhal_pwm_signal` | [`sentinel_cyhal_pwm_signal.hpp`](sentinel_cyhal_pwm_signal.hpp) | `pwm_signal` |
| `cyhal_timer` | [`sentinel_cyhal_timer.hpp`](sentinel_cyhal_timer.hpp) | `timer` |
| `cyhal_gpio_line` | [`sentinel_cyhal_gpio_line.hpp`](sentinel_cyhal_gpio_line.hpp) | `gpio_line` (via `cyhal_gpio_line_context`) |

## Bus transport vs. direct transport

The two `*_bus_transport` classes don't drive the peripheral inline — they build a
request and hand it to a **bus-arbiter task**, then block on their own response
queue. That serializes access so several driver consumers on different tasks share
one physical bus without each holding a peripheral lock. The production device
context ([`resource/`](../../resource/)) wires drivers over the **bus** transports;
the direct `cyhal_i2c_transport` / `cyhal_spi_transport` exist for single-owner or
pre-scheduler use.

See [**Bus arbiter task model**](../../README.md#bus-arbiter-task-model) for the
request/response sequence.

## See also

- [`platform_agnostic/`](../platform_agnostic/) — the interfaces these implement.
- [`task/`](../../task/) — the `i2c_bus` / `spi_bus` arbiter tasks the bus transports feed.
- [`../README.md`](../README.md) — transport overview · [`../../README.md`](../../README.md) — source-tree map.
