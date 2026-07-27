///
/// \file    sentinel_post.hpp
/// \brief   Power-On Self-Test (POST) — boot-time hardware self-check (#35)
///
/// \details Implements the Power-On Self-Test (firmware #35): a short,
///          deterministic self-test that runs once at boot — after
///          \c cybsp_init() and \c peripheral_initialize() but before the
///          application enters its main loop — and verifies each external
///          sensor / storage / clock subsystem is reachable and responsive.
///          The aggregate result and any individual subsystem failures are
///          recorded to the System Event Log (#34) as a permanent boot-time
///          record, making POST the *first real producer* of System Event Log
///          records (\c post_passed / \c post_subsystem_failed).
///
///          The goal is early, structured failure visibility: a unit returning
///          from the factory with a bad solder joint on the BME280's SDA line
///          logs a \c post_subsystem_failed for the BME280 subsystem at every
///          boot, which the iOS client can read out over BLE the moment the
///          device is paired — no UART, no guessing.
///
///          === Probe / aggregate / record separation ===
///
///          POST is split into three layers so the *logic* is unit-testable
///          off-bench while the *probing* runs against real hardware on-bench:
///
///          1. \b Probes — one templated \c probe_*() per subsystem, duck-typed
///             on the driver it tests (the same pattern as the drivers and the
///             event log). Each returns a
///             \ref sentinel::diagnostics::post_subsystem_result mapping a
///             driver outcome onto a \ref sentinel::diagnostics::post_subsystem
///             / \ref sentinel::diagnostics::post_result pair. Because they are
///             duck-typed, the testbench drives them with
///             tiny fake driver doubles to exercise every result code
///             deterministically (mirroring each hardware acceptance
///             criterion).
///          2. \b Aggregate — \ref sentinel::diagnostics::post::run() calls
///             every probe in turn and accumulates the results into a
///             \ref sentinel::diagnostics::post::summary (\c all_passed /
///             \c failure_count / a sentinel-terminated result array).
///             \ref sentinel::diagnostics::post::summary::add is pure logic.
///          3. \b Record — \ref sentinel::diagnostics::post::record_results()
///             emits the summary to a duck-typed event log. All-pass emits one
///             \c post_passed; any failure emits one \c post_subsystem_failed
///             per failed subsystem.
///
///          === Degraded-operation contract ===
///
///          POST never halts boot. A failing subsystem is recorded and the
///          application proceeds without it (degraded operation is preferred
///          over a boot-loop). If the *record store itself* fails POST, no
///          event can be persisted; \ref
///          sentinel::diagnostics::post::record_results() then falls back to
///          the BLE debug stream (#25) — the only logging path that does not
///          depend on flash — and skips the (futile) event-log writes.
///
///          === Read-only record-store probe (deviation from the #35 sketch)
///          ===
///
///          The issue sketch had the record-store probe write a throwaway test
///          record and read it back. That would pollute the System Event Log
///          with a stray record on every boot. Instead
///          \ref sentinel::diagnostics::post::probe_record_store is read-only
///          (initialize + sane head/tail/capacity), and the SPI + flash +
///          record-store write path is validated end-to-end by the very next
///          step — \ref sentinel::diagnostics::post::record_results() writing
///          POST's own real result records through the log. Same coverage, no
///          pollution.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - Power-On Self-Test (firmware #35)
///

#ifndef SENTINEL_POST_HPP
#define SENTINEL_POST_HPP

#include "sentinel_debug_print.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace sentinel::diagnostics {

///
/// \brief Every subsystem POST can probe.
///
/// \details The underlying type is \c uint8_t so a subsystem id costs one byte
///          in a \c post_subsystem_failed record. These values are a permanent
///          wire contract (shared with the BLE GATT retrieval path #6 and the
///          iOS client): append-only, never reused, never renumbered. The
///          0x10+ range is reserved for Phase II/III application peripherals.
///
enum class post_subsystem : uint8_t {
    bme280 = 0x01,
    ds3231 = 0x02,
    w25q128 = 0x03,
    record_store = 0x04, ///< Log header readable + a record writable?
    ble_stack = 0x05,    ///< Stack init ok + GATT database registered?

    // Application-specific (Phase II/III).
    rotary_encoder = 0x10,
    display = 0x11,

    invalid = 0xFF, ///< Sentinel terminating
                    ///< \ref sentinel::diagnostics::post::summary::results.
};

///
/// \brief Outcome of a single subsystem probe.
///
/// \details Like \ref sentinel::diagnostics::post_subsystem, these are an
///          append-only wire contract.
///          \c pass is 0 so a zeroed record reads as "passed".
///
enum class post_result : uint8_t {
    pass = 0x00,
    fail_no_ack = 0x01,    ///< Device did not respond on the bus.
    fail_wrong_id = 0x02,  ///< Device responded with the wrong chip id.
    fail_self_test = 0x03, ///< Device's own internal self-test failed.
    fail_timeout = 0x04,   ///< Device responded but the operation timed out.
    fail_init = 0x05,      ///< Initialization sequence returned an error.
};

///
/// \brief One subsystem's POST outcome.
///
/// \details \c error_detail is subsystem-specific: the read chip-id byte for a
///          \c fail_wrong_id from the BME280, the manufacturer byte for the
///          W25Q128, 0 where there is nothing useful to carry.
///
struct post_subsystem_result {
    post_subsystem subsystem; ///< Subsystem this result is for.
    post_result result;       ///< Outcome code for \c subsystem.
    uint8_t error_detail;     ///< Subsystem-specific detail byte (0 if
                              ///< unused; see the struct \details).
};

///
/// \brief Expected BME280 chip-id (register 0xD0). A working part always
///        returns this; defined locally to keep the heavy Bosch C headers out
///        of this header.
///
inline constexpr uint8_t kBme280ChipId = 0x60u;

///
/// \brief Power-On Self-Test driver: probes, aggregation, and recording.
///
class post {
public:
    /// Maximum number of subsystem results a single POST run can hold.
    static constexpr std::size_t kMaxResults = 16u;

    ///
    /// \brief Accumulated result of one POST run.
    ///
    /// \details \c results is sentinel-terminated by a
    ///          \c post_subsystem::invalid entry so a reader that does not
    ///          have \c count (e.g. a future on-wire decoder) can still iterate
    ///          it. \c count is kept for O(1) appends.
    ///
    struct summary {
        bool all_passed{true};    ///< \c true iff every probe passed.
        uint8_t failure_count{0}; ///< Failed-probe count (saturating).
        uint8_t count{0};         ///< Entries appended so far.
        std::array<post_subsystem_result, kMaxResults>
            results{}; ///< Sentinel-terminated result array (see the struct
                       ///< description).

        /// \brief Construct an empty summary with a sentinel-filled result
        /// array.
        summary() noexcept {
            results.fill(post_subsystem_result{post_subsystem::invalid,
                                               post_result::pass, 0u});
        }

        ///
        /// \brief Append one subsystem result, updating the aggregate flags and
        ///        keeping the array sentinel-terminated. Silently ignored once
        ///        full.
        ///
        /// \param r The subsystem result to append.
        ///
        void add(const post_subsystem_result &r) noexcept {
            if (count >= kMaxResults) {
                return;
            }
            results[count] = r;
            count++;
            if (r.result != post_result::pass) {
                all_passed = false;
                if (failure_count < 0xFFu) {
                    failure_count++;
                }
            }
            if (count < kMaxResults) {
                results[count] = post_subsystem_result{post_subsystem::invalid,
                                                       post_result::pass, 0u};
            }
        }
    };

    // =====================================================================
    // Per-subsystem probes (duck-typed on the driver under test)
    // =====================================================================

    ///
    /// \brief Probe the BME280: read chip-id (reg 0xD0), expect \ref
    ///        kBme280ChipId. No response → \c fail_no_ack; wrong id →
    ///        \c fail_wrong_id with the read byte as detail.
    ///
    /// \param dev BME280 driver instance to probe.
    /// \return The subsystem result (\c post_subsystem::bme280).
    ///
    template <typename Bme280>
    static post_subsystem_result probe_bme280(const Bme280 &dev) noexcept {
        const auto id = dev.read_chip_id();
        if (!id) {
            return {post_subsystem::bme280, post_result::fail_no_ack, 0u};
        }
        if (*id != kBme280ChipId) {
            return {post_subsystem::bme280, post_result::fail_wrong_id, *id};
        }
        return {post_subsystem::bme280, post_result::pass, 0u};
    }

    ///
    /// \brief Probe the DS3231: read the oscillator-stop flag. No response →
    ///        \c fail_no_ack; OSF set → \c fail_self_test (the RTC lost power
    ///        without a battery; the flag is cleared so the next boot reads
    ///        clean).
    ///
    /// \param dev DS3231 driver instance to probe.
    /// \return The subsystem result (\c post_subsystem::ds3231).
    ///
    template <typename Ds3231>
    static post_subsystem_result probe_ds3231(Ds3231 &dev) noexcept {
        const auto osf = dev.oscillator_stop_flag();
        if (!osf) {
            return {post_subsystem::ds3231, post_result::fail_no_ack, 0u};
        }
        if (*osf) {
            dev.clear_oscillator_stop_flag();
            return {post_subsystem::ds3231, post_result::fail_self_test, 0u};
        }
        return {post_subsystem::ds3231, post_result::pass, 0u};
    }

    ///
    /// \brief Probe the W25Q128: read the JEDEC id, expect any known-good
    ///        triple. No response → \c fail_no_ack; unknown id →
    ///        \c fail_wrong_id with the manufacturer byte as detail.
    ///
    /// \param dev W25Q128 driver instance to probe.
    /// \return The subsystem result (\c post_subsystem::w25q128).
    ///
    template <typename Flash>
    static post_subsystem_result probe_w25q128(const Flash &dev) noexcept {
        const auto id = dev.jedec_id();
        if (!id) {
            return {post_subsystem::w25q128, post_result::fail_no_ack, 0u};
        }
        if (!Flash::is_known_jedec(*id)) {
            return {post_subsystem::w25q128, post_result::fail_wrong_id,
                    id->manufacturer};
        }
        return {post_subsystem::w25q128, post_result::pass, 0u};
    }

    ///
    /// \brief Probe the record store: \c initialize() succeeds and head/tail/
    ///        capacity are self-consistent (read-only — see the file header for
    ///        why no throwaway record is written). Init failure → \c fail_init;
    ///        inconsistent indices → \c fail_self_test.
    ///
    /// \param store Record store instance to probe.
    /// \return The subsystem result (\c post_subsystem::record_store).
    ///
    template <typename Store>
    static post_subsystem_result probe_record_store(Store &store) noexcept {
        // Re-scanning the backing flash region is O(capacity) — ~8 k serialized
        // SPI reads for the production event-log region (tracked for
        // optimization in issue #49). The boot orchestrator already initializes
        // the store before POST runs, so trust an already-initialized store and
        // only verify its geometry; a fresh store (the off-bench fakes, or a
        // skipped boot init) is initialized here as before.
        if (!store.initialized() && !store.initialize()) {
            return {post_subsystem::record_store, post_result::fail_init, 0u};
        }
        const auto head = store.head_index();
        const auto tail = store.tail_index();
        const auto cap = store.capacity();
        if (cap == 0u || head < tail || (head - tail) > cap) {
            return {post_subsystem::record_store, post_result::fail_self_test,
                    0u};
        }
        return {post_subsystem::record_store, post_result::pass, 0u};
    }

    ///
    /// \brief Probe the BLE stack from the init results captured at boot.
    ///        Stack init failed → \c fail_init; GATT database not registered →
    ///        \c fail_self_test.
    ///
    /// \param stack_ok   Whether the BLE stack initialized successfully.
    /// \param gatt_db_ok Whether the GATT database registered successfully.
    /// \return The subsystem result (\c post_subsystem::ble_stack).
    ///
    static post_subsystem_result probe_ble_stack(bool stack_ok,
                                                 bool gatt_db_ok) noexcept {
        if (!stack_ok) {
            return {post_subsystem::ble_stack, post_result::fail_init, 0u};
        }
        if (!gatt_db_ok) {
            return {post_subsystem::ble_stack, post_result::fail_self_test, 0u};
        }
        return {post_subsystem::ble_stack, post_result::pass, 0u};
    }

    // =====================================================================
    // Aggregate + record
    // =====================================================================

    ///
    /// \brief Run every subsystem probe and return the accumulated summary.
    ///
    /// \details Probes are run in a fixed order (BME280, DS3231, W25Q128,
    ///          record store, BLE stack). Each is a single bus exchange with no
    ///          retry loop, so a healthy or failing pass is equally fast. The
    ///          BLE stack status is captured by the caller at stack-init time
    ///          and passed in (POST runs before the scheduler starts, so it
    ///          cannot itself drive the stack).
    ///
    /// \param bme           BME280 driver instance to probe.
    /// \param rtc           DS3231 driver instance to probe.
    /// \param flash         W25Q128 driver instance to probe.
    /// \param store         Record store instance to probe.
    /// \param ble_stack_ok  Whether the BLE stack initialized successfully.
    /// \param gatt_db_ok    Whether the GATT database registered successfully.
    /// \return The accumulated \ref summary of every probe.
    ///
    template <typename Bme280, typename Ds3231, typename Flash, typename Store>
    static summary run(const Bme280 &bme, Ds3231 &rtc, const Flash &flash,
                       Store &store, bool ble_stack_ok,
                       bool gatt_db_ok) noexcept {
        auto s = summary{};
        s.add(probe_bme280(bme));
        s.add(probe_ds3231(rtc));
        s.add(probe_w25q128(flash));
        s.add(probe_record_store(store));
        s.add(probe_ble_stack(ble_stack_ok, gatt_db_ok));
        return s;
    }

    ///
    /// \brief Record a POST summary to a duck-typed event \p log.
    ///
    /// \details All-pass emits a single \c post_passed. Otherwise one
    ///          \c post_subsystem_failed is emitted per failed subsystem,
    ///          carrying its subsystem id / result / detail. Every failure is
    ///          additionally mirrored to the BLE debug stream. If the record
    ///          store itself failed POST, the event-log writes would be lost,
    ///          so they are skipped and the debug stream is the only record.
    ///
    /// \param log A \ref sentinel::diagnostics::system_event_log (or any type
    ///            exposing \c record_post_passed / \c
    ///            record_post_subsystem_fail).
    /// \param s   The summary returned by \ref run().
    ///
    template <typename Log>
    static void record_results(Log &log, const summary &s) noexcept {
        if (s.all_passed) {
            log.record_post_passed();
            logi("post: all subsystems passed");
            return;
        }

        const auto store_ok = record_store_healthy(s);
        for (auto i = uint8_t{0}; i < s.count; i++) {
            const auto &r = s.results[i];
            if (r.result == post_result::pass) {
                continue;
            }
            if (store_ok) {
                log.record_post_subsystem_fail(
                    static_cast<uint8_t>(r.subsystem),
                    static_cast<uint8_t>(r.result), r.error_detail);
            }
            loge("post: subsystem failed (subsys=0x%02X result=0x%02X "
                 "detail=0x%02X)",
                 static_cast<unsigned>(static_cast<uint8_t>(r.subsystem)),
                 static_cast<unsigned>(static_cast<uint8_t>(r.result)),
                 static_cast<unsigned>(r.error_detail));
        }
    }

private:
    ///
    /// \brief Did the record store pass POST (so event-log writes can persist)?
    ///        \c true if the store was not probed at all.
    ///
    /// \param s The summary returned by \ref run().
    /// \return \c true if the record store passed (or was not probed).
    ///
    static bool record_store_healthy(const summary &s) noexcept {
        for (auto i = uint8_t{0}; i < s.count; i++) {
            if (s.results[i].subsystem == post_subsystem::record_store) {
                return s.results[i].result == post_result::pass;
            }
        }
        return true;
    }
};

} // namespace sentinel::diagnostics

#endif /* SENTINEL_POST_HPP */
