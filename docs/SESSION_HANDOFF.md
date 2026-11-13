# Session Handoff

Rolling status for AI assistants (Claude, etc.) and human collaborators who join
partway through: **what just merged, what's in play, what's next.** Durable
reference lives in [`docs/architecture/`](architecture/) — see the pointers
below. Update this file at the end of any session; keep it lean (~150 lines) by
pushing decisions / infra / as-built detail into the architecture docs rather
than letting them accumulate here.

## Durable reference (read these for the "why")

- [`architecture/decisions.md`](architecture/decisions.md) — **cumulative
  architectural decisions (#1–#16…)**, referenced by number across the codebase.
- [`architecture/github-infrastructure.md`](architecture/github-infrastructure.md)
  — project board ID + all field/option IDs, milestones, labels, `gh` recipes,
  and the **contribution rules** (branch workflow, commit/issue conventions).
- [`architecture/hardware-bench.md`](architecture/hardware-bench.md) — board,
  bus pinouts, wiring gotchas, local build/flash quick reference.
- [`architecture/repo-layout.md`](architecture/repo-layout.md) — source tree map.

---

**Last updated:** 2026-11-13. **Firmware 1.0.2.0 RELEASED (#78)** — tagged
`v1.0.2`, squash-merged `release/1.0.2` → `develop` → `main`; pre-squash history
at tag `release-1.0.2-history`. GitHub Release v1.0.2 created as a **draft**
(review + publish). Phase I firmware work is done; the next stretch is
**client-only** (see "Client follow-ups" below).

Landed in this final stretch (all bench-validated on the CYBLE-416045):
- **#86** full 16-bit build in event records + real OTA target in `attempted`.
- **#87** paged reads: a Record Block response longer than **one LL packet
  (246 B)** arrived corrupted past byte 246 (MTU 517), silently dropping records
  7–13 of every 14-record page (and 3 of 6 snapshots). Blocks are now capped at
  `min(MaxAttrLength, MTU-1, 246)`; root cause in the BT stack → **#88 (Phase II)**.
- **#89** boot snapshot junk (25.1 °C / 91.3 % / 645.6 hPa): `cyhal_system_delay_us`
  takes `uint16_t`, truncating the BME280's 112.8 ms conversion wait to 47 ms.
  New `sentinel::cyhal_delay_us()` (RTOS ms + chunked µs) in all CYHAL
  transports; RTC temp read on first tick; boot snapshot waits for real readings.
- **#71** OTA interruption matrix: reset/power cut mid-upload → old image boots,
  logged `failed` reason **5 (interrupted)** via a new marker **stage** byte
  (`0xFF` downloading → `0x7F` staged; was mis-logged `reverted`); phone BT off
  mid-upload → `failed` reason 2, clean reset; **power cut during the MCUBoot
  overwrite → bootloader finishes the copy, new image boots** (no brick).
- **#94** RTC: the DS3231 restarts at 2000-01-01 on power loss (no working
  backup battery). Time is trusted only while OSF is clear; untrusted → Unix
  Time reads **0** and records are stamped 0 until a BLE time-set clears OSF.
  POST's ds3231 probe **no longer clears OSF** (it used to, so only the first
  boot after a power loss knew).
- **#80** shipped as-is; remaining event types tracked in #82, #90–#93, #18.
- **#53 / #57** end-of-Phase-I sweep: tree clang-format clean, Doxygen 0
  warnings, READMEs current.

**Bench gotchas learned:** the testbench's `time_sync_from_build` writes the
build's *local* time into the RTC (the app treats it as UTC) — set the time
from the client after a testbench run. Pulling USB resets the DS3231 (check its
coin cell). Switching `TESTBENCH` wipes `build/`; a `qprogram` during a rebuild
fails with "couldn't open …hex" (harmless, nothing written).

**Client follow-ups (sentinel-client, Phase I):** cl#58 ("Recovering…" hangs —
unbounded ABORT write in `OTAUploader.recover()`), cl#55 (phone Bluetooth off
not detected mid-DFU), cl#61 (keep screen awake during DFU), cl#60 (return to
MainTabView after DFU), cl#62 (auto-set time when Unix Time reads 0), cl#54
(Logs auto-refresh), plus mirroring fw#86 `*_build_hi` and reason 5.

**Phase II firmware backlog from this release:** #88 (multi-packet BLE
responses), #82 (fault events), #90–#93 (config / pairing / shutdown /
runtime-state events), and the HCI 0x28 ("instant passed") disconnects right
after OTA PREPARE (~2 of 6 DFUs; the 384 KB secondary-slot erase likely stalls
the link) — not yet filed.

---

**Earlier 2026-11-13:** **#85 overwrite upgrade — COMMITTED + bench-validated;
#86 full-width versions — COMMITTED, bench-pending.** Both on `release/1.0.2`.

- **#85 (`3805a5c`, `517fc5c`):** MCUBoot switched from swap-using-scratch (~24
  erase/copy passes through a 16 KiB scratch, tens of seconds) to **overwrite-only**
  (one erase + copy). Project-local flashmap `Sentinel/psoc63_1m_cm0_int_overwrite_single.json`
  (no scratch/swap-status, same slots); `OTA_FLASH_MAP` + `scripts/build-mcuboot-app-*.sh`
  point at it; `flashmap.mk` (tracked) regenerated with `USE_OVERWRITE := 1`.
  **Gotchas:** deleting `flashmap.mk` before a build trips the vendor Makefile's
  late-include-after-`DEFINES` ordering (`MCUBOOT_IMAGE_NUMBER` expands empty →
  `sysflash.h` "no left operand") — regenerate it with `flashmap.py` instead.
  Signing now passes `--overwrite-only` (via `CY_SIGNING_KEY_ARG`); without it the
  ~18 KB swap trailer made the Debug testbench (0x5d56b) exceed the 0x60000 slot.
  The **bootloader must be re-flashed** with the overwrite MCUBootApp. Bench:
  single pass, much faster boot. #81 `reverted` now means "MCUBoot rejected the
  image" rather than a rollback.
- **#86 (`258b0fc`):** versions lost the build's high byte (4-byte compact → wraps
  at build 256; #81 could call 111→367 a revert). Additive fix in the zeroed
  reserved tails: `boot_lifecycle_record.build_hi`,
  `firmware_update_record.{from,to}_build_hi`; marker keeps the high byte in its
  old reserved word (`from_build_full()`); resolver compares full versions.
  `firmware_update_attempted` is now written when the MCUBoot header's `ih_ver`
  arrives, so it carries the real target (failures before that still write
  attempted first). Old records/markers decode unchanged. **Client mirror
  pending** (combine `*_build_hi`). Testbench + app build/sign clean; Doxygen 0.
- **Client findings (filed):** post-DFU Logs list is stale because `LogsView` reads
  once and the fast overwrite reboot lets the app reconnect mid-boot before the
  boot records drain (cl#54 comment); return-to-scan after a successful DFU (cl#60).
- **Build note:** switching `TESTBENCH` wipes `build/` (`.last_testbench`) — build
  the image you want to flash last.
- **Open:** RTC drifted to 1999 once (DS3231 lost time — unfiled); 51.0.2.111-session
  records missing from one downgrade-time list (check after cl#54 refresh fix).

**Last updated:** 2026-11-13. **#81 OTA: emit firmware_update_completed / _reverted
via a boot-time swap-confirm detector — COMMITTED on `release/1.0.2` (`474d9cc`),
bench-pending.** A truthful `0x05`/`0x07` is only knowable at the *next* boot (after the
post-download reset + the MCUBoot swap). New `storage::firmware_update_marker` — a fixed
16-byte record in its own 4 KiB W25Q128 sector (`0x0F0000`, below the event log, outside
both circular stores; `set`/`load`/`clear`). OTA PREPARE persists the marker (`from` =
running version; target unknown here, derived at boot). The boot orchestrator resolves it
right after the flash stores init: a *different* running version →
`firmware_update_completed` (from→running), the *same* version → `firmware_reverted`;
then clears it. A clean boot logs neither. `ota_fail_and_reset` (#84) and the user ABORT
now **clear** the marker — a failure is already logged as `firmware_update_failed`, never
double-counted as a revert; the success reboot (OTA confirmation handler) leaves it
intact, so it survives exactly the swap-triggering reset. Decision factored into pure
`diagnostics::resolve_post_swap_event()` with a 5-case testbench suite (upgrade /
downgrade / minor / build-only → completed; same-version → reverted). **No GATT contract
change** (log contents only) — client #54 lights up once `0x05` is emitted. Build-verified
(testbench links clean; only the known-benign imgtool/`click` signing POSTBUILD is
unrelated). **Board:** #81 → In Review; #83/#84/#71/#80 → In Review; **#82 (fault family)
deferred to Phase II** (the compelling faults — thermal/over-current/actuator — need Phase
II peripherals). **Client is in sync** (`develop` == `main`, both at origin). **Before the
cut:** build a signed #81 image, bench-validate the completed/reverted events (DFU swap →
next boot logs `0x05`), then #78 (1.0.2.0 cut).

**Last updated:** 2026-11-13. **#84 OTA: reset to a clean state on a failed/interrupted
update — COMMITTED on `release/1.0.2` (`64b4db2`), bench-pending.** Follow-up to #83.
**Bench finding:** a DFU interrupted mid-download (ordinary BLE link drop → client
`operationCancelled`) left the device *alive but in a fragile half-state* (agent
half-torn-down by #83's disconnect handler, secondary slot erased/open, same boot
session, re-advertising) that the client could not cleanly reconnect to — it hung on
"Recovering…". Confirmed this is **not** a #83 regression (serial showed
`cy_ota_agent_start() DONE`, no "already created") and **not** a downgrade rejection
(cy_ota has no version check; MCUBoot downgrade-prevention off). **Fix:** new
`ble_context::ota_fail_and_reset(reason)` (log `firmware_update_failed` → `ota_cleanup`
→ 1 s guard → `NVIC_SystemReset`) on the three *failure* paths — signal-loss disconnect,
verify-fail, agent-start-fail — so a fresh boot brings every task + GATT service back.
A deliberate **user cancel** stays in-place (no reboot; works now that #83 clears the
agent singleton). Added reason `OTA_FAIL_START = 4`. Build-verified Release (clean; only
the pre-existing RWX linker warning). **Downstream client half:
`galudino/sentinel-client#52`** (bound the reconnect so the app never hangs). **The
`#83`/`#71` 2nd-attempt "OTA context already created!" bug is confirmed FIXED on bench**
(`edd8bcf`). **Also this session:** built two WIP A/B DFU test images from `edd8bcf`
(`1.0.2.107-wip` + `51.0.2.107-wip`, major+50) and refreshed the client bundle
(`galudino/sentinel-client#51`); the `51.0.2.107` swap was validated end-to-end on bench.
Still open before the cut: re-flash `64b4db2`, bench-validate #84 (interrupt → reboot →
re-advertise → client reconnects), then #78 (1.0.2.0 cut).

**Last updated:** 2026-10-20. **1.0.2.0 is in progress on `release/1.0.2`**
(off `develop`), tracking [#78](https://github.com/galudino/sentinel-firmware/issues/78).
Version is still stamped **1.0.2.92** (`34deea6`) — **re-stamp `BUILD` before the
next bench flash / at the cut**. `1.0.2` is **not cut** (no `v1.0.2` tag).
Phase I firmware is **not locked**: client work may still surface firmware fixes.

**All 1.0.2 code is now on the branch; what remains is on-device validation.**
`#76` landed this session (`9390933`) — it was written in a prior session but
left uncommitted in the working tree; build-verified (firmware Release links +
OTA-signs clean) and committed. firmware Release builds + signs clean;
`doxygen Doxyfile` = **0 warnings** (re-verified, incl. the #76 files — needed a
qualified `\ref`, `a28cbde`); clang-format clean across all 114 `src/` files
(#53); READMEs + the nRF Connect checklist refreshed (#57). **#53 + #57 are
software-complete; both re-verified against the #76 code.**

- **#76 — large paged reads slow / torn-slot hid history** (`9390933`). Four
  changes: (1) `resource::configure_spi_clock` reprograms the SCB SPI flash
  divider from the configurator default (~100 kHz) to **4 MHz** — each flash
  read is µs not ms; (2) `ble_context` requests a **15 ms** connection interval
  on connect (central may refuse); (3) `gatt::paged::fill_block` **skips** a
  torn/unreadable slot (bounded by `MAX_SKIPPED_SLOTS`=64) instead of ending the
  block — one bad record no longer hides the rest; client dedupes the
  re-delivered successor by timestamp; (4) `ble_gatt` read at `offset == cur_len`
  reads empty per ATT, not `INVALID_OFFSET`. **Bench (decisive diagnostic):**
  rebuild a large store, reset, read `boot: snapshot history ready (ok=1, N
  records)` — N correct **and** the client's Trends/History populate at scale.
  ⚠️ **the 100 kHz→4 MHz jump will be far less forgiving of the marginal
  breadboard SPI wiring** — if flash reads flake, re-seat/shorten SPI wiring
  before suspecting the code.

**Committed this session — NOT yet validated on hardware (flash, check, close):**

- **#75 — Record Count notify** (`086b3aa`). Both paged Record Count chars are
  R/Notify with a CCCD (`design.cybt`). `gatt::paged::publish_{snapshot,event}_count`
  fire on every append and on Clear Store — i.e. **per record, not per value
  change**, so a full (wrapped) store whose count is pinned at capacity still
  signals new records. Event log stays BLE-agnostic via
  `system_event_log::set_appended_hook` (bound in the boot orchestrator; boot-
  sequence records don't fire it). Audit: no other read-only-dynamic char needs
  notify. **Bench:** subscribe to each count; watch a notification per persisted
  snapshot / logged event. Watch the event-log drain task's stack (it now sends
  the notification; `configMINIMAL_STACK_SIZE * 4`).
- **#73 — Firmware Build Info** (`5f049c9`). New System char
  `354E9885-A000-44FA-AB17-7251002BAAB1`, read-only 3 B `[channel, config,
  variant]`; `sentinel::build_info` (`utilities/sentinel_build_info.hpp`).
  `SENTINEL_BUILD_CHANNEL?=wip` in the Makefile (`rc` / `release` at a cut —
  **set `release` for the 1.0.2.0 build**). Blocks client #42 (native
  blocked-by link set). **Bench:** read `02 00 00` on a default Release app build.
- **#74 — clock seeded early** + **#68 — second-change gate** (`ac421e7`).
  `rtc_service::seed_time()` runs right after the device context is built.
  rtc_service acts once per RTC second. **Bench:** boot log + first event/snapshot
  records carry real time; testbench shows one heartbeat/s. #68's *root cause*
  (scope P6[3], read back DS3231 `0x0E`, external pull-up) is still a hardware
  task — the firmware gate only bounds the symptom.
- **#79** (advertise indefinitely, `e949f32`) — still awaiting its bench check.

**Handles renumbered** across this release (two CCCDs + Build Info added for
#75/#73; Request Bootloader Mode **removed** for #71) — the client discovers by
UUID, so only the contract mirror matters: client #43 (count notify), #42
(build info), and the `requestBootloaderMode` removal (below).

**#71 (OTA interruption / power-loss) is hardware-only** — nothing left to code.
**Decision taken 2026-10-20: `Request Bootloader Mode` REMOVED** (`04caed2`) — it
was vestigial for OTA (the client defined `requestBootloaderMode()` but never
called it; OTA runs in-app and MCUBoot swaps on the post-download reset; the char
was just a delayed `NVIC_SystemReset`). Dropped from `design.cybt` + the write
handler + `ble_maintenance_task`. **Client must mirror the removal** (owner is
handling it). The rest of #71 is the bench interruption/power-loss matrix.

**Next session (BENCH — critical path):** flash the Release app + testbench →
validate the whole committed set **#75/#73/#74/#68/#79/#76** → run the **#71**
interruption/power-loss matrix → close issues → cut 1.0.2.0 per #78's checklist
(re-stamp BUILD,
`SENTINEL_BUILD_CHANNEL=release`, tag the pre-squash tip, squash
`release/1.0.2`→`develop`→`main`, tag `v1.0.2`, GitHub Release with signed
assets, sync the client bundle). **All software for 1.0.2 is now committed and
verified — nothing left to code; the release gate is purely on-bench.**

**Previously landed + validated on `release/1.0.2`:** #72 (paged reads) + #69
(device readiness).

The two validated fixes:

- **#72 — paged GATT block reads returned `INVALID_OFFSET` on blob reads**
  (`ce7d0d8`). `before_read()` refilled the record block from the live circular
  store on *every* ATT read, including CoreBluetooth's Read Blob continuations, so
  a multi-payload block shifted mid-read (→ `WICED_BT_GATT_INVALID_OFFSET`, or a
  block stitched from two store states). Fix: thread `read_request->offset` into
  `before_read` and refill only when `offset == 0`. **Test on hardware:** Logs tab
  loads the 8000+ event log with no "offset is invalid"; Home Climate
  Readings/Trends + RTC populate from Snapshot History.
- **#69 — Device Readiness (boot/POST state) on the System service** (`36ee693`).
  New 2-byte `[state, failedSubsystem]` R/Notify characteristic
  (`D3E542A5-3476-4A48-B4F5-1D0D0699DB28`), added to `design.cybt` (GeneratedSource
  regenerates via `make build`'s `bt-configurator-cli` step — it is gitignored, so
  only `design.cybt` is tracked). `diagnostics::device_readiness` enum;
  `gatt::system::{set,notify,publish}_readiness`; boot orchestrator publishes
  booting → post_running → ready/degraded(subsystem). POST-duration audit (AC):
  POST is already sub-100 ms, so no optimization needed. **Test on hardware:** the
  client #38 (branch `38-device-readiness-gate`) consumes it — connect mid-boot and
  watch Home gate on readiness. Board: #72/#69 → In Review / Phase I, sub-issues of
  #4.

> **cybt regen note:** editing `Sentinel/src/design.cybt` (e.g. adding a
> characteristic) and running `make build` triggers the configurator's
> `bt-configurator-cli` code-gen step, which regenerates `GeneratedSource/` and
> renumbers handles automatically. The standalone `bt-configurator` GUI binary
> won't code-gen headless on macOS (only the `cocoa` Qt plugin; `--output-dir`
> writes nothing). Use `make build`.

---

**Prior:** 2026-08-19 — **1.0.1 cut + RELEASED: record-store resilience +
build-script fixes.** First patch on the public 1.0.0 baseline. Squash-merged to
`develop` and `main`, tagged **`v1.0.1`**, and published as a **[GitHub Release](https://github.com/galudino/sentinel-firmware/releases/tag/v1.0.1)**
with 4 signed assets (combined MCUBoot+app hex, OTA `.bin`, `.hex`, `.elf`). Version
bumped in `Sentinel/Makefile` (`OTA_APP_VERSION_PATCH=1`; BUILD stays CI-owned).
Testbench **48/48 on hardware**. The client bundles the 1.0.1 `.bin` as stock
firmware (+ a `255.255.255-test` image built via `OTA_APP_VERSION_*=255` override).
**Firmware is FROZEN again** (public stable release; touch only for critical fixes).

> **Bench note:** a stray uncommitted VSCode drag moved `src/testbench/` →
> `src/test/testbench/`, which silently **broke the `TESTBENCH=0` firmware app
> build** (`multiple definition of sentinel::create_orchestrator` — the moved
> orchestrator leaked into the app build). It was reversed; if the app build ever
> fails that way again, check for stray sources under `src/`.

- **`record_store` resilience (root-cause guard for the snapshot count underflow).**
  The device reported a garbage snapshot count (`4294967252` = a `uint32` −44):
  `initialize_full_scan()` trusted a slot that read `STATUS_VALID` over an erased
  `0xFFFFFFFF` sequence, so `head = max_seq + 1` overflowed to 0 and `count()`
  underflowed. Fix in `sentinel_record_store.hpp`: (1) a **format descriptor** in
  each region's last sector (magic + layout version + slot/record size) — on boot
  a missing/mismatched descriptor means stale/foreign flash, so the region is
  **reformatted** (`erase_all` now stamps the descriptor) instead of scanned; (2)
  the scan **skips `0xFFFFFFFF` sequences**; (3) `count()` is **clamped to
  capacity**. Costs one sector of capacity per region; first boot on 1.0.1
  reformats both stores once (heals the corrupted snapshot region). Tests: the
  record-store scratch region went 2→3 sectors; all record-store cases pass.
- **The corruption's *proximate* cause was marginal SPI wiring**, not firmware —
  the W25Q128 erase was intermittently failing/garbling on the breadboard (erase
  is the highest-current op; reads/register-writes passed). Re-seating the SPI
  power/ground fixed the bench (48/48). Added a **read-only `protection_diagnostic`**
  to the W25Q128 testbench (dumps SR1/2/3 + WEL-after-WREN) that ruled out flash
  write-protection during the hunt — kept as a permanent flash-health probe. See
  [`architecture/hardware-bench.md`](architecture/hardware-bench.md).
- **Build-script fixes (`Sentinel/scripts/`).** The two **testbench** combine
  scripts looked in `third_party/` (underscore) while MCUBootApp builds to
  `third-party/` (hyphen) → `srec_cat … No such file`. Fixed both, and hardened
  the whole 14-script build pipeline: `#!/usr/bin/env bash` + `set -eo pipefail`
  (genuine failures now abort instead of silently falling through to a misleading
  combine error), and the mcuboot toolchain path is now env-overridable
  (`MCUBOOT_GCC_PATH`, `$HOME` default) for CI. Verified end-to-end: the full
  `build-combined-…-testbench-release.sh` produces the combined image, exit 0.

---

**Last updated:** 2026-08-18 — **#67 BLE advertising fix landed on `develop`
(the sanctioned break in the freeze — it blocks the client), and CI #60 scoped.**

- **`d1408d9` #67 — service-filtered discovery:** the board was undiscoverable by
  an iOS `scanForPeripherals(withServices:)` filtered on a Sentinel service — the
  advertisement only carried the 16-bit Battery UUID (0x180F), none of the custom
  128-bit service UUIDs. Fix (`design.cybt` + regenerated `cycfg_gap.c`, which is
  a gitignored build product): advertise **Flags + the System 128-bit UUID**
  (`11A3488D-…-F166`) in the **primary** packet, move the Complete Local Name to a
  new **scan-response** packet, drop the dead **Appearance** (0x0000); primary-packet
  budget 35 → 21 B. `sentinel_ble_context.cpp`: added
  `wiced_bt_ble_set_raw_scan_response_data(...)` in `ble_start_advertising()`
  (disconnect re-advertise was already wired). **Regen is headless** —
  `bt-configurator/bt-configurator-cli -c src/design.cybt -o src/GeneratedSource`
  (no GUI; round-trips byte-for-byte). Version stays 1.0.0. Builds (link OK; the
  imgtool `click` sign step fails benignly locally). **⏭️ Remaining: on-bench
  iOS/nRF service-filtered discovery check on the System UUID.** Client side was
  narrowed to scan the System UUID only (client `develop` `6a9314f`).
- **CI #60 (Phase II) — scoped, still deferred:** confirmed CI publishes **no**
  firmware releases today (only the Pages/Doxygen workflow). Full design notes +
  the toolchain-provisioning blocker (only the Infineon-account-gated MTB *tools
  package* is non-automatable; ARM GCC, getlibs, signing venv are public) +
  artifact policy (**build Debug+Release, publish Release only**) are cataloged in
  a comment on #60. Runner choice (hosted-Linux-with-seeded-tarball vs
  self-hosted) is the open decision there.

**Last updated:** 2026-07-27 (late) — **PHASE I WRAPPED; repo is PUBLIC.**
This session: #63 OTA DFU validated + closed (3 bugs, see below), #65 Bosch
adapter, #57 READMEs, #53 Doxygen+sweep, #56/#49 flash, #6/#45/#55 GATT, #38/#51.
**Phase I milestone CLOSED (33/33 issues done); board clean (0 closed-but-not-Done).**

**⏭️ PICK UP HERE (next session):**
1. **Promote + push:** `develop` is **ahead of `main`** by the v1.0.0 versioning
   commit (`a16d232`) + this handoff. `git checkout main && git merge --no-ff
   develop && git checkout develop`, then `git push origin develop main`.
   (`develop`/`main` were reconciled to `04dc5a7` earlier this session; a stray
   run of #63 commits had landed on `main` directly — now both had all fixes.)
2. **Stand up #60 CI** (build firmware+testbench, publish `.bin`/combined `.hex`
   + signed OTA image as artifacts) using the **version wiring** below. Then #61
   (clang-format + doxygen-0-warnings gates).
3. **#66** (migrate ota-update → v4.x, retire our patch) = Phase II. (#65 AC #4
   — BME280 on the wire — was signed off during the flash-flake bench, 4/4;
   #65 is closed. Nothing else is left from Phase I.)

**🧊 FREEZE:** `sentinel-firmware` is a stable public **v1.0.0** and is being held
as-is while `sentinel-client` is the active work. **Do not touch the firmware
except for a genuinely critical fix** (build-breaker, security, or a bug that
blocks the client). All 25/2/2 open issues are Phase II/III/IV — future, not now.

**Versioning (DONE this session, `a16d232`):** default firmware version bumped
**0.0.0.1 → 1.0.0.0** (public Phase I MVP). `major.minor.patch` are **human-owned**
(bump per the `sentinel_firmware_version.hpp` guidelines: major=overhaul,
minor=related-feature-set, patch=single feature/fix); **`build` is CI-owned.**
`firmware_version::build()` was widened `uint8_t → uint16_t` (it silently truncated
the 16-bit build); `device_snapshot.firmware_build` (uint16) is now full-width.

**CI version wiring (sketch, for #60) — build number = commit count, NOT 0/1:**
```yaml
# In the firmware build job, before building:
- id: ver
  run: echo "build=$(git rev-list --count HEAD)" >> "$GITHUB_OUTPUT"
# Pass to the build (Makefile OTA_APP_VERSION_* are `?=`, so env overrides win):
env:
  OTA_APP_VERSION_BUILD: ${{ steps.ver.outputs.build }}
  # OTA_APP_VERSION_MAJOR/MINOR/PATCH: taken from Sentinel/Makefile defaults
  #   (1.0.0) — humans bump those in the Makefile (or move to a VERSION file/tag).
```
`git rev-list --count HEAD` is stateless + monotonic (a few hundred now, safe in
the uint16 field for years). It is **your commit count**, so it does not "start at
0 or 1" — it's whatever HEAD's count is. If you want it to *reset per release*,
use `git rev-list --count <last-version-tag>..HEAD` (0 right after you tag
`v1.0.0`). Recommended: total count. **imgtool build is uint32** (huge headroom);
the on-device `firmware_version` build is uint16 — keep the CI build < 65535
(commit-count easily does).

**GitHub Actions / Pages (FIXED this session):** all prior Actions runs failed at
the **Deploy-to-Pages** step because Pages was never enabled (was gated on the
repo being public — #62). Now public → enabled Pages with **source = GitHub
Actions** (`POST /repos/.../pages -f build_type=workflow`); re-ran the Docs
workflow → **success**. Doxygen site live at
**https://galudino.github.io/sentinel-firmware/**. #62 can be closed.

**#63 — OTA DFU (BLE MCUBoot) VALIDATED + CLOSED.** Firmware OTA'd over BLE
(AIROC `btsdk-peer-apps-ota` iOS host) → MCUBoot swap → new image booted
(version + marker confirmed) → full POST pass. **Three bugs fixed en route:**
1. **CCCD clobbered at PREPARE** (`5e66aa6`) — `ble_context::ota_value_initialize()`
   zeroed `m_ota_config_descriptor` (set by the client's control-point CCCD
   write) right before `cy_ota_ble_download_prepare()` read it → aborted at 0%.
   Fix: init the CCCD once in `default_value_initialize()`, not per-PREPARE.
2. **ota-update v2.0.0 `write_data_to_flash()` flash-row-boundary bug** — a BLE
   (row-unaligned) chunk crossing a `CY_FLASH_SIZEOF_ROW` (4096) boundary
   overflowed the one-row scratch buffer + dropped the spill-over bytes → the
   OTA'd secondary-slot image was corrupted → MCUBoot rejected the swap (hash
   mismatch). Root-caused with an **instrumented MCUBoot** (a temporary
   `#63 HASH MISMATCH/SIG CHECK` printf in `bootutil` loader/image_validate —
   since reverted). Fixed by clamping each write to the current row. See
   [[project_ota_dfu_bugs]]. **mtb_shared is gitignored**, so the patch is
   carried by `Sentinel/scripts/apply-mtb-shared-patches.sh` +
   `Sentinel/patches/…row-boundary.patch`, and `build-sentinel-*.sh` re-apply it
   before every build (`ae32f76`). **Migration to ota-update v4.x = #66 (Phase
   II)** — v3.0.0 offloaded flash write to the app, so v4.x drops the bug + our
   patch entirely.
3. (minor) stale `srec_cat` (tools_3.5) + `TARGET_→APP_` build-artifact paths in
   the combine/build scripts (`81e105f`).
Diagnostic gotcha worth keeping: the BLE OTA "CRC Verification Succeeded" is
computed over the received **RAM** buffer, NOT re-read from flash — so it does
**not** catch a bad flash write; only MCUBoot's post-reboot hash does. The
"battery-server worked but Sentinel didn't" was a red herring: identical library/
config; the bug fires only when the runtime chunk stride straddles a 4096 row
(Sentinel's was 244 B). **NEXT: on-bench BME280 re-verify for #65 (AC #4).**

**#57 (this session — merged `develop`, tag `57-per-directory-readmes-history`;
CLOSED):** every meaningful `src/` subdirectory now has a `README.md` fanning out
from the `src/README.md` keystone — 22 pages (app, resource, drivers + 7 driver
subdirs, transport + 2, task, storage, diagnostics, telemetry, bluetooth, logging,
utilities, test, testbench). Uniform shape: purpose → key types/entry points →
usage snippet → cross-links. All reachable from `src/README.md` (no orphans); 286
relative links verified. Snippets checked against real APIs (e.g.
`create_orchestrator`, `record_store` ctor, `jedec_id()`/`is_known_jedec`,
`device_snapshot::populate(out)`, `current_firmware_version`). Vendored/generated
dirs left undocumented by design.

**#65 (this session — merged `develop`, tag `65-bosch-adapter-history`; issue
closed):** the `byte_transport` CRTP base + all three CYHAL transports carried
static `bosch_read`/`bosch_write`/`bosch_delay` (Bosch Sensortec function-pointer
ABI) — an inverted dependency (reusable transport depending on a sensor-specific
vendor lib). Moved the **entire** adapter into `bme280<Transport>` as private
statics that cast `intf_ptr` back to `Transport*` and branch the register framing
on the compile-time `is_i2c`/`is_spi` tag over the transport's generic
`read`/`write`/`write_read`/`delay_us`. Transports are now vendor-agnostic
byte-movers — **`grep bosch` under `transport/` is empty**. Wire framing preserved
byte-for-byte on the bme280 I²C bus-transport path (net −201 LOC: one adapter
replaces four duplicated copies). **Builds:** testbench release + firmware release
both link + OTA-sign clean. **On-bench BME280 re-verification PENDING** (AC #4 —
this changes the bytes on the wire: chip-id 0x60, soft-reset, settings round-trip,
a plausible T/H/P sample).

**#53 sweep (this session, merged `5cdb265`, tag `53-format-sweep-history`):**
`clang-format -i` across `Sentinel/src`; reviewer rules — `RecordT`→`RecordType`,
classes defined inside `namespace sentinel {}` (no out-of-line qualified defs),
for-loop postfix / statement prefix, and **record-store reads C-style out-param
→ `std::optional<T>`** (`record_store`/`ram_record_store`/`system_event_log`/
`snapshot_persistence`/`gatt_paged` read chain; `read_range` kept its
caller-array buffer form). Verified: testbench debug + firmware release
(OTA-signed) link; `doxygen Doxyfile` = 0 warnings. Delivered partly via
subagent fan-out (test call sites + namespace-nested), parent-fixed the bme280
alias-ordering + missed `run_boot_sequence` reads that the build caught.

**New issues filed this session:** #60 (CI: build + artifacts), #61 (CI:
clang-format + doxygen-0-warnings gates — the *enforcement* so docs/style never
regress, replacing per-phase cleanup), #62 (enable GitHub Pages — needs repo
public, gated on #53/#57), #63 (**OTA DFU validation vs a known-good Infineon
host** — Phase I finale; client DFU is a separate `sentinel-client` concern),
#64 (evaluate `std::expected`/value-or-error error handling — C++23, so a
`__cplusplus`-gated `result<T,E>` alias for the RPi5/nRF5340 ports).

**Docs are decoupled by design:** Doxygen = code-only API reference (`INPUT =
Sentinel/src`); the narrative Markdown (#57 READMEs, `docs/architecture/`)
renders on GitHub. Doxygen HTML is gitignored + published by CI (never committed
to source). See [[reference_doxygen]].

**THIS SESSION (2026-07-27):**
- **#53 Doxygen backfill — DONE (merged `90c42dc`, tag
  `53-doxygen-backfill-history`).** `doxygen Doxyfile` emits **0 warnings**
  tree-wide (was 1,248). Every entity in `Sentinel/src` documented; Doxyfile is
  now a **code-only** build (`INPUT = Sentinel/src`; narrative Markdown renders
  on GitHub per #57; `__attribute__(x)` stripped; vendored `bosch/`/
  `app_bt_utils`/`cy_ota_config`/`*driver_file_template*` excluded). Delivered
  by a 6-way subagent fan-out + parent mop-up. Comments only; both builds link.
  **⚠️ Doxygen verify:** run `doxygen Doxyfile` **as-is** (HTML on) — the
  `GENERATE_HTML=NO` shortcut hits a 1.11.0 false-positive on member
  `\param`/`\return`. #53 stays **In Progress** for its *other* half: the
  end-of-Phase-I `clang-format -i` + reviewer-rule sweep (`RecordT`→
  `RecordType`, namespace-nested defs, prefix/postfix, ref-for-mutation).
- **#57 — `Sentinel/src/README.md` keystone merged (`d7ba68e`).** Module map +
  4 Mermaid diagrams. Remaining: per-directory README fan-out (In Progress).
- **#58 (User button SW2/P0_4 driver) + #59 (on-demand diagnostics service =
  extended POST, button + GATT triggered, non-destructive) — filed, Phase II
  Backlog.** The exhaustive destructive testbench stays a separate app
  (decision #15); #59 is the safe, live-unit "more involved POST".

**⚠️ RECURRED 2026-07-27 (post-#65 bench):** the marginal SPI-flash connection is
back — W25Q128 3/5 (`erase_program_read` "post-erase not blank, last_err=0";
`security_register_round_trip` byte mismatch) + record_store 0/7 (garbage
head/tail from the init scan; read `-4`). **Not a code regression:** #65 never
touched the flash path (W25Q128/record_store run over `cyhal_spi_bus_transport`,
untouched; the direct `cyhal_spi_transport` #65 edited is instantiated nowhere),
and **BME280 passed 4/4 on the wire — #65 AC #4 SIGNED OFF**. Physical signature
confirmed: (a) RAM-backed `system_event_log` **8/8** = record_store logic is fine,
only the flash instance fails; (b) corruption **moves every run** (security byte 0
then byte 11 = single-bit flip 0x67→0x77; record_store tail 15/2/128); (c) SPI
HAL returns success (`last_err=0`) but data is wrong; (d) short transactions pass
(JEDEC/SR1/power-down), long/multi-byte ones corrupt. **Now fails on both reset
AND reflash** → not a power-up/ready sequencing gap → degraded physical contact.
**RESOLVED same day → 47/47, zero code change:** moved the SPI **VCC/GND jumpers to
different sockets on the same power rail** — the prior sockets were **worn** (loose
grip). Root cause confirmed = breadboard socket contact, not the rail/leads/
decoupling. (So #65 AC #4 stands signed off; the flash suite is green again.)

**⚠️ BENCH RELIABILITY WATCH — marginal SPI (flash) connection.** The testbench
first came up with **5 failures** (W25Q128 `erase_program_read` + 4 flash-backed
`record_store` tests: `read -4`/`count=0`), then — with **zero code change**,
just re-seating SPI VCC/GND and reflashing — went 5-fail → 1-fail → **47/47**,
and has stayed 47/47 across the **three runs since (two reflashes + one bare
reset, no reflash)**. The failing test *moved* between runs and it was
**SPI-only** (I²C sensors never flaked). The clean bare-reset run makes a
firmware power-up/ready sequencing gap unlikely (it re-ran full bring-up + suite
without re-flashing). At the configured
**100 kHz** SPI clock there is no signal-margin/opcode explanation → it is a
**marginal physical contact** (loose jumper / breadboard / GND-VCC), not a
firmware bug (record_store logic is separately proven by the RAM-backed
`system_event_log` suite, 8/8 every run). **Currently seated well, NOT proven
permanently fixed.** Before trusting OTA DFU (which writes flash), harden the
flash wiring (short/solid leads, 100 nF decoupling at the module, solid GND) and
get repeatable 47/47 across several *cold power-cycles* (not just debugger
reflashes). Discriminator: if failures return only on cold boot (never on
reflash), suspect a flash power-up/ready sequencing gap in bring-up instead.

**THIS SESSION (2026-07-10, autonomous):**
- **#56 (MERGED to `develop`, squash `8ea30be`, tag
  `56-w25q128-write-completion-history`; board → In Review).** w25q128
  erase/program could report false success when the op was a silent no-op —
  completion was inferred from BUSY-clear alone, which can't tell "finished"
  from "never started". New `wait_until_write_complete()` requires **both** BUSY
  and WEL (SR1 bit 1) to clear; a completed op auto-clears WEL (JEDEC), so a
  never-started op leaves WEL latched and now times out honestly. Rewired
  `page_program` / `chip_erase` / `erase_with_address`. **On-bench pending:**
  AC #2 (deterministic `erase_program_read` across many runs) + AC #3 (a
  WEL-injection regression test — not feasible off-bench today; the suites drive
  the real device, no fake SPI transport exists).
- **#49 (MERGED to `develop`, squash `c5b625d`, tag
  `49-record-store-fast-scan-history`; board → In Review).**
  `record_store::initialize()` was an unconditional O(capacity) min/max scan
  (~8.7 s/region, ~17 s boot). Now classifies the region from **slot 0**: valid
  seq 0 → never wrapped → binary-search the valid/empty boundary
  (O(log capacity)); empty → `region_is_blank()` per-sector head probe
  (O(sector_count)) distinguishes a truly blank region (fast, the ~17 s case)
  from a power-loss-mid-recycle-of-sector-0 transient (falls through to the full
  scan so no records are lost); else wrapped → the unchanged authoritative
  `initialize_full_scan()`. No on-flash format change. New testbench
  `recycle_transient_recovery` guards the transient. **Builds validated locally
  (release firmware OTA image signs + fits; release + debug testbench link).**
  **On-bench pending:** run the record_store suite (now 7 tests → expect 7/7)
  and measure the empty-region boot scan (<1 s).
- **#53 (branch `53-doxyfile-doc-infra`, NOT merged — review; board → In
  Progress).** Added a curated repo-root `Doxyfile` (`OUTPUT_DIRECTORY =
  docs/doxygen`, gitignored; `EXTRACT_ALL=NO` + `WARN_IF_UNDOCUMENTED` so the
  coverage gap shows; MathJax, no dot). Verified `doxygen Doxyfile` runs clean
  (1.11.0, exit 0). The `clang-format -i` + reviewer-rule sweep + doc backfill
  stay the **end-of-Phase-I** task.
- **#57 (branch `57-src-readme-docs`, NOT merged — review; board → In
  Progress).** Added `Sentinel/src/README.md` — the module-map TOC + the four
  core Mermaid diagrams (CRTP transport, bus-arbiter tasks, two-lane snapshot,
  boot orchestrator). Per-directory module READMEs fan out from this anchor
  (remainder of #57, left so the pattern can be reviewed first).

**#6 / #45 / #55 (MERGED to `develop` 2026-07-10, squash `f573cb1`, history tag
`6-ble-gatt-services-phase-i-history`; issues closed).** Phase I GATT catalog +
DIS/Platform ID + PSoC 6 die-temperature, validated on-bench (nRF Connect +
testbench 46/46). As-built + durable notes:
- **GATT DB** in `src/design.cybt`, regen via `bt-configurator-cli -c src/design.cybt
  -o GeneratedSource` — the `-o` path is relative to the config's parent (`src/`),
  so NOT `-o src/GeneratedSource`; `GeneratedSource/` is gitignored + rebuilt.
  8 custom services + DIS + Battery; CUD on every custom char, CCCD only on
  notifiers. UUIDs in the #6 body (client #9 mirrors 1:1).
- **Accessor layer** (`src/bluetooth/sentinel_gatt_*.hpp`) = the single seam over
  generated `app_*`/`HDLC_*`: `inline`+`noexcept` (never `constexpr` — they read
  extern GATT-DB globals), accessor/mutator (no raw-ref), notify sender in-layer.
- **Producers wired**: BME280 Ambient Sample; DS3231 Unix Time (R/W — the write is
  the BLE time-sync path via `ctx.rtc.set_unix_time()`, **not** rtc_service, per
  `[[project_rtc_time_design]]`) / RTC Temp / Alarm Flags; Snapshot Stream (sink +
  enable char); paged Snapshot History + System Event Log; async
  `ble_maintenance_task` for Clear Store + Request Bootloader (off the BT callback);
  real `gatt_db_ok` into POST.
- **#55 die-temp**: `drivers::psoc6_die_temperature` (SAR DieTemp channel, 1.2 V BGR,
  32× avg, SFLASH dual-slope conversion; cache+throttle+mutex; SAR on free 8-bit
  divider 6). Surfaced via System **CPU Temperature** char (R/Notify),
  `cpu_die_temp_service` (heartbeat-gated log like the peers, `SENTINEL_TESTBENCH`
  gate), and a real-SAR testbench suite. ~34 °C on-bench, correct vs BME280/DS3231.
- **`device_snapshot`** fully populated (RSSI/TX-power async-cached + CPU die temp).
- Bugs fixed en route: `firmware_version::c_str()` formatted `0.0.0.1` as `0.0.01.`
  (now guarded by static_assert); `unused()` is const-ref variadic (accepts
  non-copyable). Note `firmware_version::build()` still truncates to 8 bits — use
  `array()[3]` for the full 16-bit build.
- On-bench GATT checklist:
  [`acceptance/gatt-nrf-connect-checklist.md`](acceptance/gatt-nrf-connect-checklist.md).

**NEXT (dependency-ordered):**
1. **#49** — `record_store::initialize()` is O(capacity): slow boot flash scan
   (~17 s, two region scans). Optimize the scan.
2. **#56** — w25q128 erase/program can report false success when WEL doesn't latch
   (verify WEL / that BUSY asserted before trusting BUSY-clear). Filed with diagnosis.
3. **#53** — formatting / house style / Doxygen — address after #49 + #56.
   **#57** — per-directory README docs across the tree (roadmap; complements #53).

**#38 (merged):** boot orchestrator + shared device context + lane-1 snapshot
persistence. Both configs build clean under `-Werror -Wall -Wextra -pedantic-errors`
(`TESTBENCH=1` and `TESTBENCH=0` both reach `Linking … .elf`; only the benign
imgtool/`click` signing step fails). New pieces:
- **Shared device context** (`src/resource/sentinel_device_context.hpp`) —
  `resource::context()` Meyers singleton (decision **#17**, amends #13): holds
  the shared `bme`/`rtc`/`flash` drivers + `event_store`/`snapshot_store`,
  first-constructed inside the orchestrator (post-scheduler). `initialize_stores()`
  scans both flash regions, binds the event log, sets `context_ready()`.
  `rtc_service`/`bme280_service` now **borrow** `ctx.rtc`/`ctx.bme`.
- **Production boot orchestrator** (`src/app/sentinel_boot_orchestrator.cpp`) —
  one-shot highest-prio task (twin of #48): build context → `initialize_stores`
  → `post::run` (real drivers) → cache first-fail status → `record_results`
  (enqueue) → start event-log drain task (runs boot sequence, then drains POST
  records) → start service tasks (rtc, bme280, snapshot-persistence, snapshot-
  stream, battery) → self-delete. `main()` no longer hard-asserts on BLE-stack
  failure (POST records it; degraded boot, decision #12).
- **Lane-1 persistence** (`src/task/sentinel_task_snapshot_persistence.cpp`) —
  OO singleton, ~5 min cadence, `populate→append`, `capture_now`/`count`/`read`/
  `read_range`/`erase_all`, `snapshot_persisted` heartbeat every N captures,
  first capture = boot anchor. New testbench suite drives the **real** task over
  a scratch store (decision #15); wired as the 9th orchestrator group.
- **Flash map finalized:** event log `[0x100000..0x180000)`, snapshots
  `[0x180000..0x280000)`; `populate_snapshot` now sources storage counts +
  `ble_connected` + POST status. New events `snapshot_persisted` (0x45) /
  `pre_fault_snapshot_captured` (0x46) + `snapshot_event_record`.

**ON-BENCH BRING-UP (this session, CYBLE-416045 + GD25Q128):** happy-path boot
now validated end-to-end — POST passes (per-probe serial feedback added:
`post bme280 PASS` … `post ble_stack PASS`), event log + snapshot history scan +
init, all service tasks start, RTC/BME280 continuous reads + snapshot persistence
run. **Two on-bench bugs found + fixed:**
- **Boot hung in `resource::context()`** — first C++ function-local `static`
  constructed *post-scheduler* dead-locked in `__cxa_guard_acquire` (gthread path
  unwired in this newlib/wiced port). Fix: **`-fno-threadsafe-statics`** in
  `CXXFLAGS` (decision **#18**; safe because every singleton is first-touched
  from the single orchestrator task). Also hardened the I²C transport `exchange()`
  to fail-fast on a null response queue.
- **Boot scanned the event-log region twice** (~8.7 s each) — POST's
  `probe_record_store` re-`initialize()`d an already-scanned store. Fixed: it now
  trusts an `initialized()` store. Boot flash-scan is still ~17 s (two O(capacity)
  region scans) → optimization filed as **#49**; unified portable logging facade
  filed as **#50**.
- **Testbench: only rtc_service ran, not bme280_service** — with the guard gone
  (decision #18), the two reader tasks raced to first-construct `context()`. Fixed
  (`d2a02fb`): the test orchestrator builds `context()` once up front before
  starting the readers (same single-first-touch the app already did). **Confirmed
  fixed on-bench.**

**Also this session (not #38-blocking):**
- **Hoisted `resource::system_initialize()`** (`sentinel_resource.cpp`) out of the
  two near-identical `main.cpp`/`testbench.cpp` bodies; banner uses
  `APP_NAME_STRING` (stringized). POST reports measured duration for the <100 ms
  AC. POST prints per-probe `post <subsystem> <PASS|fail_*>`.
- **`docs/acceptance/post-hardware-acceptance-checklist.md`** — the on-bench
  fault-injection checklist for #35's six ACs (this is #38's sign-off record).
- **Build via `Sentinel/scripts/build-sentinel-{firmware,testbench}-debug.sh`**
  (venv → signing works; no more `click` error). **BLE needs the Release config**
  for `sentinel-firmware`. See [[reference_local_firmware_build]].
- **#51 DONE (merged 2026-07-01):** unified entry points + de-duplicated BT/OTA
  config. One shared `src/main.cpp` calls `sentinel::create_orchestrator()` (in
  `sentinel_orchestrator_entry.hpp`), defined per-target in each orchestrator TU;
  `TESTBENCH` `CY_IGNORE`s the other dir so the linker resolves it with no
  `#ifdef`. Single canonical `src/design.cybt` + `src/cy_ota_config.h` (regen'd
  `GeneratedSource/` at `src/`, gitignored; canonical OTA config carries the
  correct `CYBLE-416045-EVAL` board name). Orchestrator files renamed to match
  their class: `sentinel_boot_orchestrator.*` / `sentinel_test_orchestrator.*`.
  Both targets link + boot on-bench (firmware boot sequence; testbench test
  tally). Also fixed a pre-existing bug: `build-sentinel-testbench-release.sh`
  had `testbench_mode=0` (built the app under the testbench name) → set to `1`.
- **#52 DONE (merged 2026-07-01):** the `TESTBENCH`-toggle footgun surfaced during
  #51. mtbninja aggregates every `build/**/local/*.o` into the link regardless of
  `CY_BUILD_LOCATION`, so coexisting firmware+testbench trees cross-link (~2000
  dup-symbol errors; pre-#51 it *silently* emitted a mislabeled binary). Per-target
  build dirs (the first idea) are impossible — mtbninja scans them all (verified
  shared/sibling/unique-parent layouts). Fix: a Makefile parse-time guard cleans
  `./build` when `TESTBENCH` changes from the last build (`build/.last_testbench`),
  gated on build/program goals. Same-target rebuilds stay incremental; only the
  firmware↔testbench switch pays a one-time rebuild. Scripts inherit it (all call
  `make build|program`).

**#38 SIGNED OFF (2026-07-01):** all six on-bench POST hardware ACs resolved —
[`docs/acceptance/post-hardware-acceptance-checklist.md`](acceptance/post-hardware-acceptance-checklist.md)
carries the evidence. **AC 1/2/4/5/6 PASS on-bench** (nominal all-pass +
`done in 2 ms`; bme280 `fail_no_ack` + degraded boot; ds3231 `fail_self_test` →
self-clears across two boots); **AC 3 documented bench-infeasible** (no swappable
non-accept-listed SPI-NOR part; logic covered off-bench by #35 `fake_flash`,
decision #15). Squash-merged → `develop`, #38 closed, board → Done.

**Decisions in play:** #13 (boot orchestrator over a shared `sentinel::resource`
device context — **realized by #38**), #14 (two-lane snapshot model — **both
lanes now built**: lane 2 #46, lane 1 #38), #15 (testbench tests REAL
components), #16 (all FreeRTOS tasks OO/class), **#17 (device context = post-
scheduler Meyers singleton, amends #13)**. Full text in
[`architecture/decisions.md`](architecture/decisions.md).

**NEXT: #49 (slow boot flash scan) + #56 (w25q128 WEL false-success) — may be
tackled autonomously — then #53 (formatting / Doxygen). #57 (per-dir READMEs) is
a new docs roadmap item. #6 / #45 / #55 merged to `develop` + closed.**

---

## Working style — sessions

**One Claude Code session per issue** is the intended cadence. A session may
span more than one issue when two issues are genuinely intertwined (e.g. a
view + the state machine that drives it), but the default is one-issue-one-
session. This keeps each session's context tight and avoids the "session got
too big and started forgetting things" failure mode.

The durable record lives in **git history + GitHub issues/project boards +
this file + `docs/architecture/` + MEMORY.md** — not in any single conversation.
Starting a fresh session loses *narrative* (the running "why" thread) but never
loses *work*. To re-acquire context at the start of a new session, the opening
prompt should be roughly:

> "Working on issue #N in sentinel-firmware. Read `docs/SESSION_HANDOFF.md`
> and the body of issue #N, list the dependencies, propose the implementation,
> then proceed."

Cross-device: `claude remote-control` (or `/remote-control` in a session)
mirrors a session to the Claude iOS app / browser via QR code while code keeps
running locally. `/config` → "Enable Remote Control for all sessions" makes it
default.

---

## Project status — one-screen summary

- **Codebase:** modern C++17 firmware targeting the Infineon CYBLE-416045-EVAL
  development kit (PSoC 6 BLE), built with ModusToolbox 3.8 + GCC ARM 11.3 +
  MCUBoot for OTA.
- **Active phase:** Phase I (MVP on Infineon CYBLE-416045)
- **Companion repo:** `sentinel-client` (SwiftUI iOS/iPadOS/macOS client) —
  separate repo, but **shares ONE merged project board** with firmware (the
  `sentinel` board, `galudino/projects/2`); see its own `docs/SESSION_HANDOFF.md`.
- **What ships in Phase I:** BME280 + DS3231 + W25Q128 telemetry over BLE GATT,
  flash-backed System Event Log, flash-backed periodic Device Snapshots, live
  Device Snapshot stream, OTA DFU via MCUBoot.
- **What's working today (merged to `main`):** all three Phase I drivers
  (#1, #5, #14, #15), both bus arbiters (#27, #28), BLE stack init (#29), debug
  stream (#25), cross-platform transport CRTP base (#26), full BSP / build / OTA
  infrastructure (#30), **#33 — flash-backed circular record store** (+ shared
  W25Q128 device mutex; all 6 ACs pass on the GD25Q128), **#34 — System Event
  Log**, **#35 — POST** (fake-driven suite; hardware ACs owned by #38),
  **#37 — BME280 sample service task + cache**, **#36 — `device_snapshot`
  struct + `populate()`** (80-byte packed, cache-backed), **#47 — all FreeRTOS
  tasks are OO/class singletons**, **#46 — live snapshot stream task (lane 2)**
  (`snapshot_stream_task`, idle-until-enabled, ~100 ms; off-bench suite passes,
  on-bench BLE-central AC owned by #6), **#48 — testbench serial bottom-up
  test orchestrator** (`test_orchestrator`; every suite is run-to-completion
  `run_all() → tally`, fixture-owned bus transports), and **#38 — boot
  orchestrator + shared device context + lane-1 snapshot persistence**
  (`resource::context()`, `app::boot_orchestrator`, `snapshot_persistence_task`;
  decision #17; both builds clean; **merged to `develop` 2026-07-01, on-bench
  POST hardware ACs signed off — AC 1/2/4/5/6 PASS, AC 3 bench-infeasible**).
- **Also merged to `develop`:** **#6 / #45 / #55** — Phase I BLE GATT services,
  Device Information Service + Platform ID, and the PSoC 6 die-temperature
  driver/service (squash `f573cb1`, tag `6-ble-gatt-services-phase-i-history`).
- **Released:** **v1.0.2 (1.0.2.0)** on `main` — Phase I firmware complete
  (#71, #80, #81, #83–#87, #89, #94, #53, #57; see the top entry).
- **What's next:** the Phase I work moves to **sentinel-client** (cl#54/#55/
  #58/#60/#61/#62 + the fw#86 mirror). Firmware stays frozen except critical
  Phase I fixes; Phase II backlog: #88, #82, #90–#93, CI #60.

---

## Next issues

1. **Client-side DFU robustness** (sentinel-client cl#58, #55, #61, #60, #62).
2. **Phase II firmware:** #88 (multi-packet BLE responses), the post-PREPARE
   0x28 disconnects (to file), #82 / #90–#93 (event families), CI #60.

Durable as-built detail lives in `docs/architecture/decisions.md` and the closed
issues #6 / #45 / #55.
