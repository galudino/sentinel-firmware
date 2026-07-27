# `storage/` — circular record stores

The persistence primitive: a fixed-capacity **circular record store** of
fixed-size records. Two backends share one interface — a flash-backed store for
production and a RAM-backed store as its test double.

| Class | File | Backing |
|---|---|---|
| `record_store<RecordType, Transport>` | [`sentinel_record_store.hpp`](sentinel_record_store.hpp) | W25Q128 SPI NOR flash region |
| `ram_record_store<RecordType>` | [`sentinel_ram_record_store.hpp`](sentinel_ram_record_store.hpp) | RAM (test double; identical API) |

Everything durable in the system is a record store: the [System Event Log](../diagnostics/)
and the [Snapshot History](../telemetry/) are both `record_store`s over distinct
flash regions.

## API

```cpp
bool                      initialize() noexcept;        // scan region, find head/tail
bool                      append(const RecordType &r);  // write next slot (wraps)
std::optional<RecordType> read(uint32_t index) const;   // oldest-relative read
uint32_t                  count() const noexcept;
bool                      erase_all() noexcept;
```

```cpp
sentinel::record_store<snapshot_record, w25q128_t> history(
    flash, /*region_offset_bytes=*/0x180000, /*region_size_bytes=*/0x100000);
history.initialize();
history.append(snap);
if (auto first = history.read(0)) { /* ... */ }
```

## Region classification (fast boot scan, #49)

`initialize()` doesn't blindly scan the whole region. It classifies from slot 0:
valid seq 0 → never wrapped → binary-search the valid/empty boundary
(O(log capacity)); empty → per-sector head probe distinguishes a truly blank
region from a power-loss-mid-recycle transient; wrapped → the authoritative full
scan. No on-flash format change.

## See also

- [`drivers/flash-memory/`](../drivers/flash-memory/) — the `w25q128` this is backed by.
- [`diagnostics/`](../diagnostics/) — `system_event_log` over a store · [`telemetry/`](../telemetry/) — snapshot history.
- [`../README.md`](../README.md) — source-tree map · [`docs/architecture/decisions.md`](../../../docs/architecture/decisions.md) (#15 real-component tests).
