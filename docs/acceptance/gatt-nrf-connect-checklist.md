# GATT services on-bench acceptance checklist (#6 / #45 / #55)

Manual conformance + acceptance pass for the Phase I GATT contract using a generic
BLE central (**nRF Connect for Mobile** recommended; LightBlue works too). This is
the on-bench sign-off record until the client (`sentinel-client #9`) automates it.

## Prerequisites

- Build + flash the **main firmware, Release** config (BLE needs Release), **not**
  the testbench: `scripts/build-sentinel-firmware-release.sh` → program.
- Serial monitor on the debug UART (115200-8N1) — the boot orchestrator + service
  tasks print `logi/logw` lines that corroborate BLE actions (e.g. the RTC being
  set after a Unix Time write).
- Bench: BME280 + DS3231 on I²C, W25Q128 on SPI, CR2032 on the DS3231.
- The device advertises as **`Sentinel`**.

## How to read this

- **Label** is the Characteristic User Description (`0x2901`) the tool shows — you
  should see this text, not a bare UUID (that's the point of the descriptors).
  Standard DIS/Battery chars get their SIG names automatically.
- All multi-byte values are **little-endian, packed** (`__attribute__((packed))`).
  Generic tools show packed structs as **raw hex** — decode against the layouts
  below. Scalars (`uint8`/`uint16`/`uint32`) read directly.
- Only **notifiers** expose a subscribe toggle (they carry a CCCD `0x2902`):
  BME280 Ambient Sample, DS3231 RTC Temperature, Snapshot Stream Current Snapshot,
  System CPU Temperature, Debug Output Stream.
- ⚠ **Request Bootloader Mode** reboots into MCUBoot — test it last.

Full UUID table: issue #6. Service/char UUIDs are 128-bit vendor UUIDs; DIS +
Battery are SIG 16-bit.

---

## 1. Discovery / advertising  (#6)

- [ ] Scan finds **`Sentinel`**; connect succeeds.
- [ ] Service discovery lists: **Device Information** (`0x180A`), **Battery**
  (`0x180F`), and the custom **System, BME280, DS3231, Snapshot Stream,
  Snapshot History, System Event Log, Debug, OTA FW Upgrade**.
- [ ] Each custom characteristic shows its **User Description** label (not just a
  UUID).

## 2. Device Information Service `0x180A`  (#45)

Read-only; SIG-named automatically.

- [ ] **Manufacturer Name** = `Infineon Technologies`  *(via `vendor_of(platform)`)*
- [ ] **Model Number** = `CYBLE-416045`
- [ ] **Hardware Revision** = `1.0`
- [ ] **Firmware Revision** = `0.0.0.1`  *(mirrors the build version string)*
- [ ] **Serial Number** = 8-hex-digit string mirroring the System serial (default `00000000`)
- [ ] **PnP ID** (7 B) reads: Vendor ID Source `0x01`, Vendor ID `0xFFFF`
  (documented placeholder), Product ID `0x0001`, Product Version `0x0001`

## 3. System service  (#6 / #45)

| Label | Props | Test | Expect |
|---|---|---|---|
| Serial Number | R/W | read; write `uint32` LE (e.g. `0A 00 00 00`); re-read; re-read **DIS Serial** | readback matches; DIS Serial mirrors (`0000000A`) |
| Firmware Version | R | read 5 B | `major, minor, patch, build_lo, build_hi` = `00 00 00 01 00` |
| Platform ID | R | read `uint8` | `0x01` (cyble_416045) |
| CPU Temperature | R/Notify | read `int16`/100 °C; subscribe | plausible die temp (≈ambient + a few °C); notifies ~2 s (#55) |
| Request Bootloader Mode | W | *(last)* write `0x01` | device resets into MCUBoot (serial shows the reset) |

- [ ] Serial R/W + DIS mirror
- [ ] Firmware Version bytes
- [ ] Platform ID `0x01`
- [ ] CPU Temperature read + notify, physically plausible (#55)
- [ ] Request Bootloader Mode reboots

## 4. BME280 service  (#6)

`bme280_sample` (8 B): `int16 temperature_centi_degC · uint16 humidity_centi_pct ·
uint32 pressure_pa`, all LE.

- [ ] **Ambient Sample** read → decode plausibly (e.g. `24.xx °C`, `4x %RH`,
  ~`101000 Pa`).
- [ ] Subscribe → notifications ~1 s; values track the serial `bme280_service` log.
- Decode example: `29 09  D0 11  7C 8B 01 00` → `0x0929=2345→23.45 °C`,
  `0x11D0=4560→45.60 %RH`, `0x00018B7C=101244 Pa`.

## 5. DS3231 service  (#6)

| Label | Props | Test | Expect |
|---|---|---|---|
| Unix Time | R/W | read `uint32` LE; **write** a current epoch | read = current epoch; after write, serial `rtc_service` prints the new time (BLE time-sync) |
| RTC Temperature | R/Notify | read `int16`/100 °C; subscribe | ~ambient + ~1 °C; matches serial `rtc_service T=` |
| Alarm Flags | R | read `uint8` | `0x00` (no alarms configured in Phase I) |

- [ ] Unix Time read
- [ ] Unix Time **write sets the RTC** (verify via serial) — the BLE time-sync AC
- [ ] RTC Temperature read + notify
- [ ] Alarm Flags read

## 6. Snapshot Stream service (lane 2)  (#6)

`device_snapshot` (80 B) — layout in §9.

- [ ] Write `0x01` to **Snapshot Notify Enable**; subscribe to **Current Device
  Snapshot** → 80-byte notifications at ~100 ms.
- [ ] Decode a snapshot: `trailer_magic` (last 2 B) = `C3 A5` (`0xA5C3`); fields
  populated (temp/humidity/pressure, rtc temp, counts, ble_connected=1,
  RSSI/tx-power non-zero while connected, cpu temp, uptime).
- [ ] Write `0x00` → stream stops (serial: `snapshot_stream: returned to idle`).

## 7. Paged services — Snapshot History + System Event Log  (#6)

Paged protocol: read **Record Count** → write **Index** (relative cursor `0..count`)
→ read **Record Block** (N packed records) → repeat.

**Snapshot History** (records = `device_snapshot`, 80 B; block ≤ 6 records / 480 B):
- [ ] Record Count reads a `uint32` (grows over time as lane-1 persists, ~5 min).
- [ ] Write Index `0`, read Record Block → whole `device_snapshot` records.
- [ ] Write **Clear Store** `0x01` → count returns to 0 (async erase; serial:
  `ble_maintenance: snapshot history cleared`).

**System Event Log** (records = `system_event_record`, 36 B; block ≤ 14 / 504 B):
- [ ] Record Count `uint32` (boot/POST events already present).
- [ ] Index + Record Block walk returns 36-B event records (header 8 B + data 28 B).
- [ ] Clear Store `0x01` → count 0 (serial: `ble_maintenance: event log cleared`).

## 8. Debug + Battery + OTA

- [ ] **Debug → Output Stream**: subscribe → receive the firmware `logi/logw` lines
  over BLE as UTF-8 (great for corroborating every other test here).
- [ ] **Battery → Battery Level**: read `uint8` %, subscribe → periodic updates.
- [ ] **OTA**: service present (Upgrade Control Point + Data). Full DFU is its own
  pass — presence check only here.

## 9. `device_snapshot` decode map (80 B, LE)

| Off | Field | Type |
|---|---|---|
| 0 | unix_timestamp | u32 |
| 4 | snapshot_version | u8 (=1) |
| 5–7 | fw major / minor / patch | u8 |
| 8 | fw build | u16 |
| 16 | temperature_001c | i16 |
| 18 | humidity_001pc | u16 |
| 20 | pressure_pa | u32 |
| 24 | rtc_temperature_001c | i16 |
| 26 | rtc_alarm_flags | u8 |
| 32 | event_log_record_count | u32 |
| 36 | snapshot_log_record_count | u32 |
| 48 | ble_connected | u8 |
| 49 | ble_tx_power_dbm | i8 (in u8) |
| 50 | ble_peer_rssi_neg | u8 (magnitude; 60 → −60 dBm) |
| 56 | cpu_temperature_001c | u16 |
| 58 | post_last_status | u8 (0 = all pass) |
| 60 | uptime_seconds | u32 |
| 78 | trailer_magic | u16 (=`0xA5C3`) |

---

## Sign-off

- Firmware build / commit: __________
- Date / tester: __________
- Result: ☐ all pass  ☐ issues (list): __________

Feeds the acceptance criteria of **#6**, **#45**, and **#55**. On completion, mirror
the confirmed contract into `sentinel-client #9`.
