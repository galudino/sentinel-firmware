# `drivers/temperature-humidity-pressure/` — Bosch BME280

Driver for the Bosch **BME280** combined temperature / barometric pressure /
humidity sensor. Works over **either I²C or SPI** — the bus is chosen at compile
time from the transport tag.

| | |
|---|---|
| **Key type** | `sentinel::bme280<Transport>` · aliases `bme280_i2c<T>` / `bme280_spi<T>` |
| **Files** | [`sentinel_bme280.hpp`](sentinel_bme280.hpp) · [`sentinel_bme280.cpp`](sentinel_bme280.cpp) |
| **Vendor** | [`bosch/`](bosch/) — the official Bosch Sensortec C driver (vendored, unmodified) |
| **Issues** | #14 (I²C), #2 (SPI mode), #65 (Bosch adapter ownership) |

## API (selected)

```cpp
std::optional<uint8_t>      read_chip_id() const noexcept;    // expect 0x60
std::optional<bme280_data>  read_sensor_data() const noexcept; // one-shot T/P/H
bool                        soft_reset() const noexcept;
int8_t                      last_error() const noexcept;       // raw Bosch code
```

```cpp
sentinel::bme280_i2c<cyhal_i2c_bus_transport> sensor(bus, BME280_I2C_ADDR_PRIM);
if (auto s = sensor.read_sensor_data()) {
    // s->temperature (°C), s->pressure (Pa), s->humidity (%RH)
}
```

## Owns its Bosch adapter (#65)

`bme280<Transport>` wraps the vendored Bosch C driver. The Bosch function-pointer
callback adapter (`bosch_read` / `bosch_write` / `bosch_delay`) lives **in this
driver** as private statics that frame the register access (I²C `[reg, data]` /
repeated-start read; SPI direction-bit `reg | 0x80` / `reg & 0x7F`) and forward
the bytes to the [transport](../../transport/)'s generic byte I/O. The transports
stay vendor-agnostic — the sensor's C-library shape never leaks into them
(dependency-inversion fix, #65). Compile-time `is_i2c` / `is_spi` selects the
framing.

## See also

- [`transport/`](../../transport/) — the vendor-agnostic byte-mover it forwards to.
- [`task/`](../../task/) — `bme280_service` (periodic sampling + BLE stream).
- [`../README.md`](../README.md) — drivers overview · [`../../README.md`](../../README.md) — source-tree map.
