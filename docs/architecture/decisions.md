# Architectural decisions (cumulative)

A living, append-only log of the project's architectural decisions. Referenced
by number throughout the codebase, commits, and issues ("decision #14"); add new
decisions at the end, never renumber. Rolled out of `docs/SESSION_HANDOFF.md`
(2026-06-29) so the handoff can stay a lean rolling-status doc — these are
**active reference**, not archived history.

---

1. **System Event Log uses TYPED-VARIANT 36-byte records, not uniform 8-byte
   records.** Common 8-byte header (timestamp + event_type + reserved), 28-byte
   per-type body with `static_assert(sizeof(...) == 36)` on every typed view.
   Phase I events span a wide richness range; 3-byte payload is too limiting.
   ~7k records / 256 KiB is still ample. (#34)
2. **ONE event log, not two.** Periodic structured state is a *separate*
   dedicated log (#38) of `device_snapshot` records. Discrete events vs periodic
   state are separated by purpose, not by record format.
3. **#33 (record store) is the shared foundation — AS BUILT.** Both #34 (events)
   and #38 (snapshots) consume `sentinel::record_store<RecordT, Transport>`
   (`Sentinel/src/storage/sentinel_record_store.hpp`) on top of the W25Q128.
   Header-only template, duck-typed like the drivers. Key as-built facts that
   differ from the original #33 sketch:
   - **Signature is `<RecordT, Transport>`, capacity is a runtime ctor arg**
     (`record_store(flash&, region_offset_bytes, region_size_bytes)`), not a
     `CapacityRecords` template param.
   - **Each slot carries a 4-byte monotonic `sequence`** (8-byte header:
     status + 3 reserved + sequence) so head/tail recover correctly *after
     wrap* — status bytes alone can't tell newest from oldest once every slot
     is `0xA5`.
   - **Slots are power-of-two sized** (`next_pow2(8 + sizeof(RecordT))`), so a
     slot never straddles a 256 B page / 4 KiB sector and index→address is one
     multiply. Consequence: a 36-byte record → 64-byte slot (≈44% overhead);
     size flash regions accordingly (see #34 plan).
   - `append()` is a two-phase write (payload, then status commit) → power-loss
     safe. `append_uncommitted_for_test()` exists to exercise the torn-write
     path. `initialize()` scans to recover head/tail; `erase_all()` resets.
4. **W25Q128 access is serialized by a shared recursive device mutex.** The SPI
   bus arbiter only serializes individual transactions, but a flash write is a
   multi-transaction logical op (write-enable → program → poll BUSY) and the
   chip's Write Enable Latch is global state that auto-clears on completion.
   Two concurrent writers clobbered each other's WEL (observed on-bench as
   spurious test failures); a read during another task's in-flight erase
   returns garbage. Fix: `sentinel::resource::flash_device_mutex` (recursive,
   created in `peripheral_initialize`), passed to each `w25q128` instance,
   taken around **every** logical op. This is what the #34 event-log task and
   #38 snapshot task (both flash writers) require to coexist. The driver's
   mutex param defaults to `nullptr` (no locking) for single-task use.
5. **#6 (BLE GATT services Phase I) is parented under #4, not #24.** GATT
   surface area is cross-platform application scope, not Infineon-specific
   platform implementation.
6. **SCB1, not SCB2, for the SPI bus on Infineon.** SCB2 hangs at boot on this
   BSP — reproducible against the stock Infineon Hello-World template; fault
   lands in FreeRTOS `xQueueSemaphoreTake` during `cybsp_init`. Detail + the
   debugging arc captured in issue #1. SCB1 + P10[0..3] is the workaround.
7. **W25Q128 driver accepts known clones via accept-list.** Winbond, GigaDevice,
   XTX, Boya, ZBIT. Memory-type `0x40` and capacity `0x18` are stable across the
   ecosystem; only the manufacturer byte varies. The physical part on the bench
   is a **GigaDevice GD25Q128** (`0xC8 0x40 0x18`).
8. **GATT services are CHIP-NAMED, not function-named, for sensors.** The
   Phase I GATT contract (#6) exposes a `BME280` service and a `DS3231` service
   as distinct services with distinct UUIDs — NOT a single generic "Sensor"
   service. Reason: TMP117 vs DS18B20 (both temperature) need disambiguation;
   chip-specific quirks differ; firmware + client mirror each other 1:1. The
   sentinel-client side uses the same convention (`BME280Service`,
   `DS3231Service`). Aggregate/cross-cutting services keep **descriptive**
   function names: `System`, `SnapshotStream`, `SnapshotHistory`,
   `SystemEventLog`, `OTA`, `DebugStream`. (Naming locked 2026-06-09; the old
   `Debug` / `Telemetry History` names are retired.)
9. **One-shot-sample rule for sensor GATT characteristics.** When a sensor IC +
   driver return all fields in a single read, the GATT layer exposes them as
   **one packed-struct characteristic**, not one per field. BME280's `read()`
   yields T+H+P at once → a single `bme280_sample` characteristic (`int16` 0.01 °C,
   `uint16` 0.01 %RH, `uint32` Pa = 8 B), NOT three. Split only when registers are
   genuinely independent (DS3231 Unix-time / temp / alarm-flags). #6 owns the
   contract + UUIDs; client #9 mirrors it 1:1.
10. **Device identity = standard DIS + machine-stable `platform_id` enum (#45).**
   The standard Device Information Service (`0x180A`) carries display metadata
   (Manufacturer Name, Model Number, HW Rev, PnP ID — Phase I). The `System`
   service carries a `uint8` `platform_id` enum (`cyble_416045`/`rpi5`/`nrf5340`)
   that the client + cloud branch on — **never** the Model Number string (same
   lesson as manifest-over-filename for firmware versions). `vendor_id` is
   *derived* via `vendor_of(platform_id)`, not its own characteristic. These
   `uint8` values are a permanent **wire contract** (BLE + cloud-manifest keys):
   explicit, append-only, never reused. One canonical token set
   (`cyble-416045`/`rpi5`/`nrf5340`) spans firmware `platform:` labels ↔ GATT
   `platform_id` ↔ cloud manifest `target`.
11. **System Event Log is store-templated, clock-injected, RAM-testable (#34,
   AS BUILT).** `system_event_log<Store>` is duck-typed on its record store so
   one body runs over the flash `record_store` (app) and a `ram_record_store`
   test double (tests) with no vtable — same pattern as the drivers. Time comes
   in as a `uint32_t(*)()` callback ("unix secs, or 0"), NOT a `ds3231&`, so the
   log doesn't depend on the RTC driver and tests get deterministic timestamps.
   The full `sentinel::firmware_version` carries a baked version *string* and is
   far larger than 4 bytes, so records embed a compact 4-byte
   `firmware_version_compact` (`build` = low 8 bits). Recording is non-blocking
   (staging queue + drain task); cross-task reads need no lock (single writer,
   atomic aligned head). `ram_record_store` mirrors the flash slot layout
   (status + sequence + payload) over a **caller-owned** buffer so a fresh store
   re-scanning the same bytes faithfully emulates a warm reboot.

    **As-built API detail (for #38's boot-path wiring + #6's GATT retrieval).**
    The log lives at `src/diagnostics/sentinel_system_event_log.hpp`; singleton
    is `system_event_log<Store>::instance()`. Records are typed-variant **36-byte**
    structs in `sentinel_system_event.hpp` (8-byte header + 28-byte body; every
    typed view `static_assert`s to 36).
    - `record_*()` are **non-blocking**: stamp a timestamp (via the injected
      `uint32_t(*)()` clock), `xQueueSend` a 36-byte staging record (return
      `false` if the depth-16 queue is full); a drain task / `drain_pending()`
      does the `store.append()`. Keeps SPI off the caller's path.
    - Boot continuity: `run_boot_sequence()` synthesizes `shutdown_unexpected`
      (at the prior record's timestamp) when the last session didn't end clean,
      then appends `boot_complete` with a recovered `boot_count`.
    - Cross-task reads need **no lock** (single writer commits payload-then-head;
      aligned 32-bit head read is atomic on CM4).
    - **Per-type structs MUST stay 36 bytes** — adding a field eats reserved
      padding; the `static_assert`s enforce it. Treat the field layouts + enum
      values as an append-only wire contract shared with the iOS client.
12. **POST is probe / aggregate / record, all duck-typed (#35, AS BUILT).**
   `sentinel::diagnostics::post` (`src/diagnostics/sentinel_post.hpp`, header-
   only) splits into three testable layers: one templated `probe_*()` per
   subsystem (duck-typed on the driver — `read_chip_id`, `oscillator_stop_flag`,
   `jedec_id`+`is_known_jedec`, store `initialize`/head/tail/capacity), a pure
   `post::summary` accumulator (`all_passed` / `failure_count` / 16-slot
   `invalid`-sentinel-terminated `results`), and `record_results(log, summary)`
   that emits to a duck-typed event log. The testbench drives the probes with
   tiny **fake driver doubles** so every `post_result` code is exercised
   deterministically off-bench (the physical ACs — pull a pin, swap the flash,
   drain the RTC battery — are manual on-bench). `post_subsystem` /
   `post_result` enums are an append-only wire contract (shared with #6 + iOS
   client). **Deviation from the issue sketch:** the record-store probe is
   read-only (no throwaway test record → no log pollution); the SPI+flash+store
   write path is instead validated by `record_results` writing POST's own real
   result records. On all-pass → one `post_passed`; per failure → one
   `post_subsystem_failed`. If the *record store itself* failed POST, event-log
   writes are skipped (futile) and the BLE debug stream (#25) is the only sink.
   **App boot-wiring deferred → see decision #13:** POST is NOT wired from a
   pre-scheduler `main()` as this issue's wording implies. The probes do their
   I/O through the bus-arbiter tasks, which only pump once the scheduler runs,
   so POST is wired from a high-priority one-shot boot-orchestrator task over a
   shared device context, landing with #38.
13. **Boot orchestration = ONE high-priority one-shot task over a shared device
   context (PLANNED; lands with #38).** Two coupled structural moves that
   collapse POST wiring + event-log wiring + snapshot persistence into a single
   readable boot sequence on the production I/O path:
   - **Shared device context.** The sensor/storage drivers (`bme280`, `ds3231`,
     `w25q128`), the flash record stores (event-log + snapshot `record_store`),
     and the `system_event_log` become application-scoped singletons owned by
     `sentinel::resource` and borrowed by reference — exactly how the cyhal SCB
     bus handles + `flash_device_mutex` already live there. Stops every task
     `new`-ing up its own driver. Motivation: (a) the BME280 ctor reads factory
     calibration into per-instance `calib_data` (`sentinel_bme280.hpp`), so N
     instances = N redundant calibration reads + N cached copies; (b) consumers
     multiply fast (POST, #37 telemetry, #38 snapshot, ~6 #6 GATT services all
     read the same sensors); (c) it completes the resource-layer shared-device
     pattern the flash mutex (decision #4) already started. This was always the
     intended shape — drivers reachable like the SCB representations.
   - **Boot-orchestrator task.** A single highest-priority one-shot FreeRTOS
     task created in `create_tasks()` that runs FIRST then self-deletes:
     `post::run(ctx…)` → `post::record_results(ctx.event_log, summary)` →
     `event_log.run_boot_sequence()` → spawn the service tasks. All boot-time
     sequencing in one function, in dependency order.
   - **Why a task, not pre-scheduler (deviation from #35's wording).** Every
     probe's I/O goes through the bus-ARBITER tasks, which block on
     `xQueueReceive(…, portMAX_DELAY)` and only pump after `vTaskStartScheduler()`.
     A literally-pre-scheduler POST can't complete one I²C/SPI transaction. The
     orchestrator honors the issue's *intent* (POST before the app does real
     work; results before service tasks spawn) while using the SAME production
     arbiter path POST validates — no parallel direct-bus code to rot. POST is
     also the event log's first writer (per the spec), so the two wire together
     here, not separately.
   - **Sequencing.** Gated on the flash map: #36 locks the event-log + snapshot
     region offsets/sizes (decision #11 left them provisional for exactly this);
     the orchestrator + device context + POST/event-log/snapshot wiring then land
     together in #38. Wiring POST alone before the map is final = throwaway
     region numbers or a half-built context #38 reopens.
14. **Device Snapshot is a TWO-LANE model over one shared `populate()`.**
   `populate_snapshot()` (#36) is the single primitive; two independent consumers
   call it at their own cadence and route to their own sink — they do NOT share a
   refresh loop (different cadence AND different gating):
   - **Lane 1 — always-on persistence (#38, PLANNED).** A task records a snapshot
     to the flash record store every ~5 min for the device's whole operational
     life, connected or not. Read back via the same paged protocol as the System
     Event Log (`SnapshotHistory` service). Mirrors #34 one-for-one.
   - **Lane 2 — on-demand live stream (#46, BUILT — `snapshot_stream_task`).** A
     dedicated normally-idle task (option a — NOT a timer inside #6) that the
     `SnapshotStream` enable characteristic (#6) wakes via `start()`. While
     enabled + connected it loops `populate()` → notify sink at ~100 ms, then
     returns to idle on `stop()`/disconnect. Producer/GATT split: the task
     produces + calls a `notify_fn` sink (set by #6); #6 owns the characteristic
     + the actual `wiced_bt_gatt` notify. Connection gating is a `connected_fn`
     predicate (default `ble_context_object.connected()`, overridable off-bench).
   - **`populate()` reads CACHES, not fresh bus I/O.** BME280 from #37's ~1 Hz
     sample cache, time from rtc_service, store counts from head/tail, BLE state,
     uptime — all cheap. This is what lets lane 2 stream at 100 ms with zero
     I²C/SPI contention, and serves lane 1 uniformly. Consequence: most fields
     refresh at ~1 Hz, so a fixed 100 ms stream sends some near-duplicate frames;
     an event-driven "notify-on-change" variant is a later refinement.
   - **Build order: #37 → #36 → {#46 stream ✓, #38 persistence} → #6.** Because
     `populate()` reads #37's cache, **#37 lands before #36** (a swap from the
     old #36-first sketch). The two lanes then consume `populate()`; #6 wires the
     producer notify-sinks to characteristics + assigns the 128-bit UUIDs.
15. **The testbench validates REAL planned components — no test-specific
   doubles for on-bench ACs.** Fake driver doubles (e.g. the #35 POST suite's
   `fake_bme280`/`fake_store`) are for **off-bench LOGIC** only — they prove a
   pure algorithm forwards the right bytes deterministically on a host. A
   component's **on-bench acceptance criteria** are validated by running it
   through its **real** wiring / consumers, never a throwaway harness built just
   to exercise it. Consequences:
   - **#35 POST hardware ACs are owned by #38.** A true on-bench POST must run
     the real BME280/DS3231/W25Q128 through the real boot orchestrator + shared
     device context — which *is* #38. So all six physical ACs (`all_pass_path`,
     `bme280_disconnect`, `w25q128_unknown_jedec`, `oscillator_stop`,
     `degraded_operation`, `timing < 100 ms`) are validated under #38, not via a
     temporary POST harness. (#35 stays closed on off-bench-green; #38 carries
     the bench sign-off.)
   - **#36 `populate()` ACs** (`populate_default`/`populate_partial`) validate
     via the real consumers (#46 stream / #38 persistence calling it on-bench),
     not a populate-runner harness.
   - **#37 ACs** split: `task_create_success` / `bus_coexistence` /
     `bme280_disconnect_resilience` are observable now from the running service
     task; `sample_period_change` / `subscriber_notify` / `mutex_correctness`
     validate when their real consumers (#6 GATT characteristic, #46 stream)
     exercise the API.
   - **#46 ACs** are covered off-bench by a behavioral suite driving the real
     `snapshot_stream_task` singleton (counting sink + controllable connection
     predicate); the on-bench BLE-central AC is owned by #6 when it attaches the
     real notify sink.
   - **Board pattern:** an issue that cannot proceed until a dependency is
     complete goes to **On Hold/Blocked**. Keep board status in sync with reality.
16. **All FreeRTOS tasks are OO (class) style.** Tasks under `Sentinel/src/task/`
   had drifted into two styles: OO classes (the bus arbiters `i2c_bus`/`spi_bus`,
   #27/#28) and procedural namespaces-of-free-functions with the task's state in
   `.cpp` **file-static globals** (`battery_service`, `debug_stream`,
   `rtc_service`, and the original `bme280_service`). Standardize on OO: a class
   per task, state (caches, mutexes, notify queues, handles) as **private
   members**, `task_create()` + accessors as members, the loop as a private
   `run()` behind a static trampoline — mirroring the bus arbiters and the class
   sketches the issues already use (#37/#38/#46). **No runtime cost**
   (singleton-with-members vs namespace-with-statics → identical static storage);
   pure encapsulation/consistency. #47 did the conversion; **every new task is
   written OO from the start.** Tasks live in `sentinel::task` (e.g.
   `snapshot_stream_task` from #46, even where an issue sketch said
   `sentinel::app`). When conventions conflict, flag it — don't silently pick one.
17. **Shared device context is a post-scheduler Meyers singleton, not file-scope
   inline objects (#38, AS BUILT — amends #13).** Decision #13 sketched the
   shared drivers as `inline` objects at file scope in `sentinel::resource`,
   "the same way the cyhal SCB bus handles live there." That cannot work
   literally: the `bme280` constructor reads factory calibration over I²C, and
   **all** bus I/O is serviced by the bus-arbiter tasks, which only pump after
   `vTaskStartScheduler()`. A file-scope object is constructed during C++ static
   init — before `main()` starts the scheduler — so its calibration read would
   block forever on an arbiter that never runs. As built, the context is a
   function-local `static` reached via `sentinel::resource::context()`
   (`src/resource/sentinel_device_context.hpp`), **first touched from the boot-
   orchestrator task** (production) / test orchestrator (testbench) — i.e. post-
   scheduler, on the real arbiter path, exactly like the #48 testbench fixtures.
   End state matches #13's intent (one instance owned by `resource`, borrowed by
   reference: `ctx.bme` / `ctx.rtc` / `ctx.flash` / `ctx.event_store` /
   `ctx.snapshot_store`); only the construction *moment* differs.
   - `resource::context()` builds the drivers + stores; `initialize_stores()`
     scans both flash regions, binds the `system_event_log`, and sets
     `context_ready()` so `populate_snapshot` reads the store counts only when
     they are meaningful (it never forces a pre-orchestrator construction).
   - `rtc_service` / `bme280_service` now **borrow** `ctx.rtc` / `ctx.bme` in
     their `run()` instead of constructing task-local drivers. `battery_service`
     is BLE-only (no shared driver) and is untouched.
   - **Flash map finalized** (decision #11 left it provisional): event log
     `[0x100000..0x180000)` 512 KiB, snapshots `[0x180000..0x280000)` 1 MiB,
     non-overlapping (`static_assert` in the context header); testbench scratch
     regions stay high (record_store 0xF00000, snapshot-suite 0xF02000, w25q128
     0xFFF000).
   - **Boot-event ordering deviation.** `post::record_results` *enqueues* the
     POST records (non-blocking); the event-log drain task then runs
     `run_boot_sequence()` FIRST (direct flash appends) and drains the POST
     records AFTER. This reads the prior session's last flash record *before*
     this boot's POST records are persisted, so `shutdown_unexpected` carries the
     correct prior-crash timestamp — at the cost of POST records landing just
     after `boot_complete` rather than before it. Correct crash attribution beats
     #13's literal "POST is the first writer" wording.
