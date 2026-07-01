# Deferred acceptance criteria — Phase I review

A single running list of acceptance criteria that are **implemented and
build/logic-verified**, but whose **final on-bench sign-off is deferred to the
Phase I feature review** (issue #4). Rationale (2026-07-01): Phase I is nearly
feature-complete; rather than gate each issue's merge on a full on-bench pass, we
close issues on build + off-bench-logic green and collect the physical/on-bench
checks here to run together at the review.

Chosen over one tracking issue per deferred AC (too much board noise) and over
burying notes in #4 (not discoverable). This doc is linked from #4 and the
session handoff. Converting any row into its own issue later is trivial.

**Legend:** ☐ not yet done · ☑ done/observed on-bench · ⚠ may be bench-infeasible

---

## #35 — POST (owned by #38)
Full step-by-step in
[`post-hardware-acceptance-checklist.md`](post-hardware-acceptance-checklist.md).
- ☐ `all_pass_path` · `bme280_disconnect` · `oscillator_stop` ·
  `degraded_operation` · `timing < 100 ms`
- ⚠ `w25q128_unknown_jedec` — needs a physically different flash; document as
  bench-infeasible if the part is soldered (logic covered off-bench by the #35
  `fake_flash` suite).

## #38 — snapshot persistence (lane 1)
Logic verified on-bench over a scratch store; these are cadence/duration
observations on the real region:
- ☐ `many_captures` — run ~10 min at a short cadence; count + monotonic
  timestamps.
- ☐ `first_capture` boot-anchor lands within `period + 1`.
- ☐ `snapshot_persisted` heartbeat emitted once per N captures (verify via the
  event log once #6's `SystemEventLog` read exists).
- ☐ `survive_reset` on the real 1 MiB region (warm reboot preserves count).

## #37 — BME280 sample service
- ☑ `task_create_success` / `bus_coexistence` / `bme280_disconnect_resilience` —
  observed on-bench (service runs, samples, survives a bad sensor).
- ☐ `sample_period_change` / `subscriber_notify` — validate via the #6 BME280
  characteristic (notify sink).
- ☐ `mutex_correctness` — validate under #6 concurrent GATT reads.

## #36 — device_snapshot populate()
- ☑ `populate_default` / `populate_partial` — observed via #38 persistence
  (well-formed snapshots captured on-bench).
- ☐ connected-central view of the live snapshot — owned by #6.

## #46 — snapshot stream (lane 2)
- ☑ off-bench behavioral suite (idle/stream/cadence/disconnect) passes.
- ☐ on-bench BLE-central AC (real notify sink at ~100 ms) — owned by #6.

## #48 — testbench serial orchestrator
- ☑ `serial_order` / `banners_and_tally` / `readers_start_after` — observed
  on-bench this session (both orchestrators run bottom-up to completion; readers
  start after; per-group + total tally prints).

## #6 / #45 — GATT + DIS (when implemented)
- ☐ Each service/characteristic read/write/notify verified against the client +
  a generic BLE tool (nRF Connect / LightBlue).
- ☐ Paged read (`SystemEventLog`, `SnapshotHistory`) walks a wrapped store
  correctly end-to-end.
- ☐ DIS (`0x180A`) + `platform_id` readable; `vendor_of()` mapping correct.
- ☐ DS3231 `Unix Time` write sets the RTC (BLE time-sync).

---

## Sign-off
At the Phase I review, work through each ☐ on-bench, tick it, and note any ⚠ that
proved infeasible (with the off-bench coverage that stands in). When all are ☑ or
documented, Phase I (#4) is done.
