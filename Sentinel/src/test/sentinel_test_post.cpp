///
/// \file    sentinel_test_post.cpp
/// \brief   Power-On Self-Test (POST) test implementations
///
/// \details Implements the tests declared in \c sentinel_test_post.hpp. POST's
///          per-subsystem probes are duck-typed on the drivers they test, so the
///          suite drives them with tiny fake driver doubles defined in this TU.
///          Each fake exposes exactly the surface a probe touches
///          (\c read_chip_id, \c oscillator_stop_flag + \c
///          clear_oscillator_stop_flag, \c jedec_id + \c is_known_jedec,
///          \c initialize + head/tail/capacity), so the very same probe code
///          that runs against the real drivers in the application runs here
///          against deterministic inputs.
///
///          The recording path is validated against a \c fake_log that
///          captures every \c record_post_passed / \c record_post_subsystem_fail
///          call. That is the precise boundary POST owns (#35): the event log's
///          own packing + persistence is already validated by #34, so this suite
///          only needs to prove POST forwards the right subsystem / result /
///          detail bytes — and, for the degraded record-store case, that it
///          forwards nothing.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - POST test implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_debug_print.hpp"
#include "sentinel_post.hpp"
#include "sentinel_test_post.hpp"
#include "sentinel_test_result.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace {

using namespace sentinel::diagnostics;

// ---------------------------------------------------------------------------
// Fake driver doubles: each exposes only the surface its probe touches.
// ---------------------------------------------------------------------------

///
/// \brief Fake BME280 double: exposes only \c read_chip_id (the surface the
///        POST bme280 probe touches).
///
struct fake_bme280 {
    std::optional<uint8_t> id; ///< Chip ID to report; \c nullopt = no ACK.
    /// \brief Report the fake chip ID.
    /// \return \ref id, or \c std::nullopt to simulate a non-responding part.
    std::optional<uint8_t> read_chip_id() const noexcept { return id; }
};

///
/// \brief Fake DS3231 double: exposes only the oscillator-stop-flag surface
///        the POST ds3231 probe touches.
///
struct fake_ds3231 {
    std::optional<bool> osf;         ///< OSF value to report.
    bool                cleared{false}; ///< Set \c true once cleared.
    /// \brief Report the fake oscillator-stop flag.
    /// \return \ref osf.
    std::optional<bool> oscillator_stop_flag() const noexcept { return osf; }
    /// \brief Record that the probe cleared the flag.
    /// \return Always \c true.
    bool clear_oscillator_stop_flag() noexcept {
        cleared = true;
        return true;
    }
};

///
/// \brief Fake W25Q128 double: exposes only the JEDEC-ID surface the POST
///        w25q128 probe touches.
///
struct fake_flash {
    ///
    /// \brief Minimal JEDEC ID triple mirroring \c w25q128::jedec_id_data.
    ///
    struct jedec {
        uint8_t manufacturer; ///< Manufacturer byte.
        uint8_t memory_type;  ///< Memory-type byte.
        uint8_t capacity;     ///< Capacity byte.
    };
    std::optional<jedec> id; ///< ID to report; \c nullopt = no ACK.
    /// \brief Report the fake JEDEC ID.
    /// \return \ref id, or \c std::nullopt to simulate a non-responding part.
    std::optional<jedec>      jedec_id() const noexcept { return id; }
    /// \brief Test whether \p j matches a known-good Winbond/GigaDevice triple.
    /// \param j JEDEC ID triple to test.
    /// \return \c true if \p j is recognized; \c false otherwise.
    static bool is_known_jedec(const jedec &j) noexcept {
        // Winbond / GigaDevice are enough to exercise the known/unknown split.
        return (j.manufacturer == 0xEFu || j.manufacturer == 0xC8u) &&
               j.memory_type == 0x40u && j.capacity == 0x18u;
    }
};

///
/// \brief Fake record_store double: exposes only the presence-check surface
///        the POST record_store probe touches.
///
struct fake_store {
    bool     init_ok{true};   ///< Value \ref initialize returns.
    uint32_t head{0};         ///< Value reported by \ref head_index.
    uint32_t tail{0};         ///< Value reported by \ref tail_index.
    uint32_t cap{100};        ///< Value reported by \ref capacity.
    bool     inited{false};   ///< Set to \ref init_ok after \ref initialize.
    /// \brief Simulate initialization, succeeding/failing per \ref init_ok.
    /// \return \ref init_ok.
    bool     initialize() noexcept { inited = init_ok; return init_ok; }
    /// \brief Report whether \ref initialize has run successfully.
    /// \return \ref inited.
    bool     initialized() const noexcept { return inited; }
    /// \brief Report the fake head index.
    /// \return \ref head.
    uint32_t head_index() const noexcept { return head; }
    /// \brief Report the fake tail index.
    /// \return \ref tail.
    uint32_t tail_index() const noexcept { return tail; }
    /// \brief Report the fake capacity.
    /// \return \ref cap.
    uint32_t capacity() const noexcept { return cap; }
};

///
/// \brief Event-log double that captures POST's recording calls.
///
struct fake_log {
    ///
    /// \brief One captured \c record_post_passed / \c
    ///        record_post_subsystem_fail call.
    ///
    struct call {
        bool    passed;    ///< \c true for a \c record_post_passed call.
        uint8_t subsystem; ///< Subsystem byte (failure calls only).
        uint8_t result;    ///< Result byte (failure calls only).
        uint8_t detail;    ///< Detail byte (failure calls only).
    };
    std::array<call, 16> calls{}; ///< Calls captured so far, in order.
    uint8_t              n{0};    ///< Number of valid entries in \ref calls.

    /// \brief Capture a passed-POST recording call.
    /// \return Always \c true.
    bool record_post_passed() noexcept {
        if (n < calls.size()) {
            calls[n++] = call{true, 0u, 0u, 0u};
        }
        return true;
    }
    /// \brief Capture a subsystem-failure recording call.
    /// \param s Subsystem byte.
    /// \param r Result byte.
    /// \param d Detail byte.
    /// \return Always \c true.
    bool record_post_subsystem_fail(uint8_t s, uint8_t r, uint8_t d) noexcept {
        if (n < calls.size()) {
            calls[n++] = call{false, s, r, d};
        }
        return true;
    }
};

/// \brief Build a summary in which every subsystem is healthy.
/// \return A \c post::summary with \c all_passed set and every subsystem
///         reporting \c post_result::pass.
sentinel::diagnostics::post::summary all_healthy_summary() noexcept {
    auto bme   = fake_bme280{kBme280ChipId};
    auto rtc   = fake_ds3231{false};
    auto flash = fake_flash{fake_flash::jedec{0xEFu, 0x40u, 0x18u}};
    auto store = fake_store{};
    return sentinel::diagnostics::post::run(bme, rtc, flash, store,
                                            /*ble_stack_ok=*/true,
                                            /*gatt_db_ok=*/true);
}

/// \brief Find the result for \p sub in \p s, or nullptr if absent.
/// \param s   Summary to search.
/// \param sub Subsystem to look for.
/// \return Pointer to the matching result, or \c nullptr if absent.
const post_subsystem_result *
find(const sentinel::diagnostics::post::summary &s,
     post_subsystem sub) noexcept {
    for (auto i = uint8_t{0}; i < s.count; i++) {
        if (s.results[i].subsystem == sub) {
            return &s.results[i];
        }
    }
    return nullptr;
}

/// \brief Report a single test's PASS/FAIL to both the BLE stream and UART.
/// \param name   Test name.
/// \param ok     \c true if the test passed.
/// \param detail Failure reason (ignored when \p ok is \c true).
void report(const char *name, bool ok, const char *detail) noexcept {
    if (ok) {
        logi("%s PASS", name);
    } else {
        loge("%s FAIL: %s", name, detail);
    }
}

/// \brief Yield long enough for the BLE debug ring buffer to drain.
/// \param milliseconds Delay duration, in milliseconds.
void yield_for_debug_drain(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

// ---------------------------------------------------------------------------
// Test bodies: return true on success, write a short reason to *why on failure.
// ---------------------------------------------------------------------------

/// \brief Every healthy probe passes; one \c post_passed record is written.
/// \param why Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_all_pass_path(const char **why) {
    const auto s = all_healthy_summary();
    if (!s.all_passed || s.failure_count != 0u) {
        *why = "summary not clean";
        return false;
    }
    if (s.count != 5u) {
        *why = "not all subsystems probed";
        return false;
    }

    auto log = fake_log{};
    sentinel::diagnostics::post::record_results(log, s);
    if (log.n != 1u || !log.calls[0].passed) {
        *why = "expected one post_passed";
        return false;
    }
    return true;
}

/// \brief A non-responding BME280 yields \c fail_no_ack for that subsystem.
/// \param why Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_bme280_disconnect(const char **why) {
    auto bme   = fake_bme280{std::nullopt}; // no ACK
    auto rtc   = fake_ds3231{false};
    auto flash = fake_flash{fake_flash::jedec{0xEFu, 0x40u, 0x18u}};
    auto store = fake_store{};
    const auto s =
        sentinel::diagnostics::post::run(bme, rtc, flash, store, true, true);

    const auto *r = find(s, post_subsystem::bme280);
    if (r == nullptr || r->result != post_result::fail_no_ack) {
        *why = "bme280 not fail_no_ack";
        return false;
    }
    if (s.all_passed || s.failure_count != 1u) {
        *why = "summary did not flag the failure";
        return false;
    }
    return true;
}

/// \brief An unknown JEDEC id yields \c fail_wrong_id with the manufacturer
///        byte as detail.
/// \param why Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_w25q128_unknown_jedec(const char **why) {
    auto bme   = fake_bme280{kBme280ChipId};
    auto rtc   = fake_ds3231{false};
    auto flash = fake_flash{fake_flash::jedec{0x99u, 0x40u, 0x18u}}; // unknown
    auto store = fake_store{};
    const auto s =
        sentinel::diagnostics::post::run(bme, rtc, flash, store, true, true);

    const auto *r = find(s, post_subsystem::w25q128);
    if (r == nullptr || r->result != post_result::fail_wrong_id ||
        r->error_detail != 0x99u) {
        *why = "w25q128 wrong result/detail";
        return false;
    }
    return true;
}

/// \brief A set OSF yields \c fail_self_test and the probe clears the flag.
/// \param why Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_oscillator_stop(const char **why) {
    auto bme   = fake_bme280{kBme280ChipId};
    auto rtc   = fake_ds3231{true}; // OSF set
    auto flash = fake_flash{fake_flash::jedec{0xC8u, 0x40u, 0x18u}};
    auto store = fake_store{};
    const auto s =
        sentinel::diagnostics::post::run(bme, rtc, flash, store, true, true);

    const auto *r = find(s, post_subsystem::ds3231);
    if (r == nullptr || r->result != post_result::fail_self_test) {
        *why = "ds3231 not fail_self_test";
        return false;
    }
    if (!rtc.cleared) {
        *why = "OSF not cleared";
        return false;
    }
    return true;
}

/// \brief One forced failure is reported and the summary still enumerates
///        every other subsystem (boot is never halted by POST).
/// \param why Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_degraded_operation(const char **why) {
    // Force a single failure (BLE stack init) and confirm POST still enumerates
    // every subsystem — it reports the failure without short-circuiting boot.
    auto bme   = fake_bme280{kBme280ChipId};
    auto rtc   = fake_ds3231{false};
    auto flash = fake_flash{fake_flash::jedec{0xEFu, 0x40u, 0x18u}};
    auto store = fake_store{};
    const auto s = sentinel::diagnostics::post::run(bme, rtc, flash, store,
                                                    /*ble_stack_ok=*/false,
                                                    /*gatt_db_ok=*/true);

    if (s.all_passed || s.failure_count != 1u || s.count != 5u) {
        *why = "did not enumerate all subsystems";
        return false;
    }
    // The four non-BLE subsystems must still read as passed.
    for (auto sub : {post_subsystem::bme280, post_subsystem::ds3231,
                     post_subsystem::w25q128, post_subsystem::record_store}) {
        const auto *r = find(s, sub);
        if (r == nullptr || r->result != post_result::pass) {
            *why = "healthy subsystem misreported";
            return false;
        }
    }
    const auto *ble = find(s, post_subsystem::ble_stack);
    if (ble == nullptr || ble->result != post_result::fail_init) {
        *why = "ble failure not recorded";
        return false;
    }
    return true;
}

/// \brief \c record_results emits exactly one \c post_subsystem_failed per
///        failure with the right fields.
/// \param why Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_records_failures(const char **why) {
    // Two failing subsystems plus a healthy record store -> two records, each
    // carrying the exact subsystem / result / detail bytes.
    auto s = sentinel::diagnostics::post::summary{};
    s.add({post_subsystem::bme280, post_result::pass, 0u});
    s.add({post_subsystem::ds3231, post_result::fail_self_test, 0u});
    s.add({post_subsystem::w25q128, post_result::fail_wrong_id, 0x99u});
    s.add({post_subsystem::record_store, post_result::pass, 0u});
    s.add({post_subsystem::ble_stack, post_result::pass, 0u});

    auto log = fake_log{};
    sentinel::diagnostics::post::record_results(log, s);

    if (log.n != 2u) {
        *why = "expected two failure records";
        return false;
    }
    if (log.calls[0].passed ||
        log.calls[0].subsystem != static_cast<uint8_t>(post_subsystem::ds3231) ||
        log.calls[0].result != static_cast<uint8_t>(post_result::fail_self_test)) {
        *why = "first failure record wrong";
        return false;
    }
    if (log.calls[1].passed ||
        log.calls[1].subsystem !=
            static_cast<uint8_t>(post_subsystem::w25q128) ||
        log.calls[1].result !=
            static_cast<uint8_t>(post_result::fail_wrong_id) ||
        log.calls[1].detail != 0x99u) {
        *why = "second failure record wrong";
        return false;
    }
    return true;
}

/// \brief When the record store itself fails, no event-log writes are
///        attempted (debug-stream-only fallback).
/// \param why Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_record_store_fallback(const char **why) {
    // When the record store itself fails POST, event-log writes are futile, so
    // record_results must not attempt any (the debug stream is the only sink).
    auto s = sentinel::diagnostics::post::summary{};
    s.add({post_subsystem::bme280, post_result::fail_no_ack, 0u});
    s.add({post_subsystem::record_store, post_result::fail_init, 0u});

    auto log = fake_log{};
    sentinel::diagnostics::post::record_results(log, s);

    if (log.n != 0u) {
        *why = "wrote to log despite store failure";
        return false;
    }
    return true;
}

/// \brief Run one test body and report its outcome.
/// \param name Test name, used for the PASS/FAIL log line.
/// \param body Test body; returns \c true on success and sets \c *why on
///             failure.
/// \return \c true if \p body passed.
bool run_one(const char *name, bool (*body)(const char **)) noexcept {
    const char *why = "assertion";
    const auto  ok  = body(&why);
    report(name, ok, why);
    return ok;
}

} // namespace

// ============================================================================
// sentinel::test::post::run_all
// ============================================================================

sentinel::test::tally sentinel::test::post::run_all() noexcept {
    auto t = sentinel::test::tally{};

    t.record(run_one("all_pass_path", body_all_pass_path));
    yield_for_debug_drain(200);

    t.record(run_one("bme280_disconnect", body_bme280_disconnect));
    yield_for_debug_drain(200);

    t.record(run_one("w25q128_unknown_jedec", body_w25q128_unknown_jedec));
    yield_for_debug_drain(200);

    t.record(run_one("oscillator_stop", body_oscillator_stop));
    yield_for_debug_drain(200);

    t.record(run_one("degraded_operation", body_degraded_operation));
    yield_for_debug_drain(200);

    t.record(run_one("records_failures", body_records_failures));
    yield_for_debug_drain(200);

    t.record(run_one("record_store_fallback", body_record_store_fallback));
    yield_for_debug_drain(200);

    return t;
}
