# POST hardware acceptance checklist (#35, signed off under #38)

The six Power-On Self-Test **hardware** acceptance criteria from #35 are validated
here, on the real bench, through the real boot orchestrator + shared device
context — **no test doubles** (decision #15). The off-bench *logic* ACs
(`records_failures`, `record_store_fallback`) are already covered by the #35
`fake_*` suite; this document is only the physical fault-injection set.

## Prerequisites

- Build + flash the **main firmware** (`make build TESTBENCH=0`), not the
  testbench.
- Serial monitor attached to the debug UART (115200-8N1). The boot orchestrator
  prints each probe as `post <subsystem> <result>` and a timing line.
- Bench: BME280 on I²C, DS3231 on I²C (+ CR2032 backup), W25Q128 on SPI.

## Evidence channels

- **Serial (primary, now):** the per-probe lines + `---- [ POST ] done in N ms: … ----`.
- **BLE debug stream:** same `logi` lines; failures also `loge` from `record_results`.
- **Event log (flash):** POST writes `post_passed` / `post_subsystem_failed`
  records. These aren't directly readable on-bench **until #6** ships the
  `SystemEventLog` GATT retrieval — so for now the serial output is the evidence,
  and the flash records are verified after #6.

Reference: probe → result mapping lives in `sentinel_post.hpp` (subsystem ids:
bme280=0x01, ds3231=0x02, w25q128=0x03, record_store=0x04, ble_stack=0x05).

---

## AC 1 — `all_pass_path` (nominal)

- **Setup:** all sensors present + healthy, DS3231 battery good, the real
  (accept-listed) W25Q128 fitted.
- **Action:** normal power-on / reset.
- **Expect (serial):**
  ```
  ---- [ POST ] ----
  post bme280 PASS
  post ds3231 PASS
  post w25q128 PASS
  post record_store PASS
  post ble_stack PASS
  ---- [ POST ] done in N ms: all subsystems passed ----
  ```
- **Recorded:** one `post_passed` event.
- **PASS if:** all five `PASS`, boot proceeds to `boot: starting service tasks…`
  and the continuous RTC/BME280 reads begin.

## AC 2 — `bme280_disconnect`

- **Setup:** disconnect the BME280 — pull its SDA (or SCL) line, or unpower the
  module. (Leave DS3231 on the bus so the I²C bus itself stays healthy.)
- **Action:** reset.
- **Expect:** `post bme280 fail_no_ack` (chip-id read gets no ACK); the other
  four still `PASS`.
- **Recorded:** `post_subsystem_failed` subsystem=`0x01` result=`fail_no_ack (0x01)`.
- **PASS if:** `bme280 fail_no_ack`, boot still completes (see AC 5).
- **After:** reconnect the BME280.

## AC 3 — `w25q128_unknown_jedec`

- **Setup:** fit a SPI-NOR flash whose JEDEC **manufacturer** byte is *not* in the
  accept-list (Winbond/GigaDevice/XTX/Boya/ZBIT — see decision #7).
- **Action:** reset.
- **Expect:** `post w25q128 fail_wrong_id` (detail byte = the unknown
  manufacturer id). Because an unrecognized flash also can't back the record
  store, expect `post record_store fail_init` too, and — since the record store
  failed — event-log writes are skipped and only the BLE debug stream carries the
  failures (this is the on-bench exercise of `record_store_fallback`).
- **Recorded:** `post_subsystem_failed` subsystem=`0x03` result=`fail_wrong_id (0x02)`
  (only if the store is healthy enough to persist — otherwise debug-stream only).
- **PASS if:** `w25q128 fail_wrong_id`, boot still completes.
- **⚠ Feasibility:** this is the hardest AC — it needs a *physically different*
  flash part (a disconnected/held-low chip reads as `fail_no_ack`, not
  `fail_wrong_id`). If the bench W25Q128 is soldered with no swappable
  alternative, note this AC as "logic covered off-bench (#35 `fake_flash`),
  hardware swap not feasible on this bench" rather than faking it in firmware.

## AC 4 — `oscillator_stop`

- **Setup:** remove the DS3231 CR2032 backup **and** cut main power so the
  oscillator stops (OSF latches). Reapply power.
- **Action:** first boot after the power loss.
- **Expect:** `post ds3231 fail_self_test` (OSF set). The probe **clears** OSF, so
  a **second** boot reads `post ds3231 PASS`.
- **Recorded:** `post_subsystem_failed` subsystem=`0x02` result=`fail_self_test (0x03)`.
- **PASS if:** `ds3231 fail_self_test` on the first post-power-loss boot, then
  `ds3231 PASS` on the next boot.
- **After:** reinsert the battery. Note the RTC time is now invalid until BLE
  time-sync (#6, deferred) — the RTC service will report an epoch-ish time.

## AC 5 — `degraded_operation`

- **Setup:** any single failure from AC 2 / 3 / 4 in place.
- **Action:** reset.
- **Expect:** POST **never halts boot** — after the failing probe line you still
  see `boot: starting service tasks…` and the continuous reads for the surviving
  subsystems.
- **PASS if:** the device boots to steady state and keeps running with the failed
  subsystem recorded, not bricked.

## AC 6 — `timing < 100 ms`

- **Setup:** nominal (AC 1).
- **Action:** read the `---- [ POST ] done in N ms: … ----` line.
- **PASS if:** `N < 100`. This times only the probe phase — the two O(capacity)
  flash-region scans run *before* POST and are excluded (they're tracked
  separately in #49). Note a *failure* path can exceed 100 ms because a
  `fail_no_ack` waits out the 100 ms I²C timeout × retries; the AC is the healthy
  path.

---

## Sign-off

When AC 1, 2, 4, 5, 6 pass (and AC 3 passes or is documented as bench-infeasible),
#38 is done: squash-merge → `develop`, close #38, board → Done. #35 stays closed
on its off-bench green; this checklist is its on-bench sign-off record.
