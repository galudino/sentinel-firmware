# `drivers/flash-memory/` — W25Q128 SPI NOR flash

Driver for the Winbond **W25Q128JV** 128 Mbit (16 MB) SPI NOR flash — the backing
store for the [System Event Log](../../diagnostics/) and [Snapshot History](../../telemetry/)
(both `record_store`s over distinct regions), and the OTA scratch.

| | |
|---|---|
| **Key type** | `sentinel::w25q128<Transport>` (SPI) |
| **Files** | [`sentinel_w25q128.hpp`](sentinel_w25q128.hpp) · [`sentinel_w25q128.cpp`](sentinel_w25q128.cpp) |
| **Issues** | #1 (driver), #33 (record store + device mutex), #56 (write-completion via BUSY+WEL) |

## API (selected)

```cpp
std::optional<jedec_id_data> jedec_id() const noexcept;           // presence check
static bool  is_known_jedec(const jedec_id_data &) noexcept;       // accept-list match
bool read_data(uint32_t address, sentinel::span<uint8_t> rx);      // read
bool page_program(uint32_t address, sentinel::span<const uint8_t> data);
bool sector_erase_4kb(uint32_t address) noexcept;                 // also 32 KB / 64 KB
bool chip_erase() noexcept;
```

```cpp
sentinel::w25q128<cyhal_spi_bus_transport> flash(bus);
if (auto id = flash.jedec_id(); id && w25q128<...>::is_known_jedec(*id)) {
    // recognised part (W25Q128JV / GD25Q128 accept-listed)
}
```

## Write completion (#56)

Erase/program completion is confirmed by **both** BUSY *and* WEL clearing, not
BUSY alone — a never-started op leaves WEL latched and now times out honestly
instead of reporting false success. Writes are guarded by a shared W25Q128 device
mutex so the two record-store regions can't collide.

> **Bench note:** intermittent SPI-flash testbench failures traced to a *marginal
> physical SPI connection* (VCC/GND), not the driver — re-seat wiring before
> chasing a driver bug. See [`docs/SESSION_HANDOFF.md`](../../../../docs/SESSION_HANDOFF.md).

## See also

- [`storage/`](../../storage/) — `record_store` built on this driver.
- [`transport/cyhal/`](../../transport/cyhal/) — `cyhal_spi_bus_transport`.
- [`../README.md`](../README.md) — drivers overview · [`../../README.md`](../../README.md) — source-tree map.
