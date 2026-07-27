# `transport/` — CRTP transport façades

The seam that makes drivers portable. A driver never touches the vendor HAL
directly; it speaks to a **compile-time-polymorphic transport façade**, so the
same driver logic runs over a CYHAL bus on the PSoC 6 today and over a POSIX/Linux
bus later (Phase III) by swapping one template argument — no `#ifdef` in the
driver.

| | |
|---|---|
| **Subdirectories** | [`platform_agnostic/`](platform_agnostic/) — the CRTP interfaces · [`cyhal/`](cyhal/) — the PSoC 6 implementations |
| **Key types** | `byte_transport<Impl, Tag>` (`i2c_tag` / `spi_tag`), `cyhal_*_transport`, `cyhal_*_bus_transport` |
| **Decisions** | [#26 CRTP transport base](../../../docs/architecture/decisions.md), [#27 / #28 bus arbiters](../../../docs/architecture/decisions.md) |

## The pattern

`byte_transport<Derived, Tag>` is a **CRTP** base — static polymorphism, no
virtual dispatch. It is specialized per protocol tag so each protocol exposes only
what makes sense for it (I²C gets addressing + STOP/repeated-start; SPI gets
full-duplex transfers). A concrete transport derives from it:

```cpp
class cyhal_i2c_bus_transport
    : public sentinel::byte_transport<cyhal_i2c_bus_transport, sentinel::i2c_tag> { ... };

// Driver is parameterised over the transport — pure logic, no HAL:
sentinel::bme280_i2c<cyhal_i2c_bus_transport> sensor(bus, 0x76);
```

The transports are **vendor-agnostic byte-movers**: they know nothing about any
sensor. (Sensor-specific glue — e.g. the Bosch C-driver adapter — lives in the
driver, not here; see #65.)

## Two flavors of concrete transport

- **`cyhal_*_transport`** — drives the peripheral inline (direct CYHAL calls).
- **`cyhal_*_bus_transport`** — routes each transaction through a **bus-arbiter
  task** ([`task/`](../task/)) so several drivers on different tasks can share one
  physical bus safely. This is what the production drivers use.

See the layering + arbiter diagrams in the source-tree map:
[**The layered picture**](../README.md#the-layered-picture) ·
[**Bus arbiter task model**](../README.md#bus-arbiter-task-model).

## See also

- [`platform_agnostic/`](platform_agnostic/) · [`cyhal/`](cyhal/)
- [`drivers/`](../drivers/) — the consumers · [`task/`](../task/) — the bus arbiters.
- [`../README.md`](../README.md) — source-tree map · [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md).
