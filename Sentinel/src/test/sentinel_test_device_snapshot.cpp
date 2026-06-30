///
/// \file    sentinel_test_device_snapshot.cpp
/// \brief   device_snapshot test implementations (#36)
///
/// \details Implements the tests declared in \c sentinel_test_device_snapshot.hpp.
///          All tests are pure and off-bench: they build a snapshot in RAM,
///          serialize it through a raw byte buffer, and assert on the bytes.
///          They mirror the harness style of the POST / event-log suites
///          (body returns bool + a failure reason; \c report logs PASS/FAIL to
///          both the BLE debug stream and the retarget-IO UART).
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - device_snapshot test implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_log.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_debug_print.hpp"
#include "sentinel_device_snapshot.hpp"
#include "sentinel_test_device_snapshot.hpp"
#include "sentinel_test_result.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

using sentinel::telemetry::device_snapshot;
using sentinel::telemetry::SNAPSHOT_TRAILER_MAGIC;
using sentinel::telemetry::SNAPSHOT_VERSION;

void report(const char *name, bool ok, const char *detail) noexcept {
    if (ok) {
        logi("%s PASS", name);
        cy_log_msg(CYLF_DEF, CY_LOG_INFO, "device_snapshot %s PASS\n", name);
    } else {
        loge("%s FAIL: %s", name, detail);
        cy_log_msg(CYLF_DEF, CY_LOG_INFO, "device_snapshot %s FAIL: %s\n", name,
                   detail);
    }
}

void yield_for_debug_drain(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

bool body_size_invariant(const char **why) {
    if (sizeof(device_snapshot) != 80u) {
        *why = "sizeof != 80";
        return false;
    }
    return true;
}

bool body_byte_layout(const char **why) {
    auto s = device_snapshot{};
    std::memset(&s, 0, sizeof(s));
    s.unix_timestamp = 0x11223344u;   // offset 0, little-endian
    s.snapshot_version = 0x55u;       // offset 4
    s.temperature_001c = 0x0102;      // offset 16, little-endian
    s.trailer_magic = SNAPSHOT_TRAILER_MAGIC; // offset 78 (0xA5C3)

    uint8_t buf[sizeof(device_snapshot)];
    std::memcpy(buf, &s, sizeof(s));

    if (buf[0] != 0x44u || buf[1] != 0x33u || buf[2] != 0x22u ||
        buf[3] != 0x11u) {
        *why = "unix_timestamp not LE at offset 0";
        return false;
    }
    if (buf[4] != 0x55u) {
        *why = "snapshot_version not at offset 4";
        return false;
    }
    if (buf[16] != 0x02u || buf[17] != 0x01u) {
        *why = "temperature_001c not LE at offset 16";
        return false;
    }
    if (buf[78] != 0xC3u || buf[79] != 0xA5u) {
        *why = "trailer_magic not LE at offset 78";
        return false;
    }
    return true;
}

bool body_round_trip(const char **why) {
    auto original = device_snapshot{};
    std::memset(&original, 0, sizeof(original));

    original.unix_timestamp = 1700000000u;
    original.snapshot_version = SNAPSHOT_VERSION;
    original.firmware_major = 2u;
    original.firmware_minor = 1u;
    original.firmware_patch = 3u;
    original.firmware_build = 1801u;
    original.temperature_001c = -2345; // -23.45 °C
    original.humidity_001pc = 5012u;   // 50.12 %RH
    original.pressure_pa = 101325u;
    original.rtc_temperature_001c = 2500;
    original.rtc_alarm_flags = 0x03u;
    original.event_log_record_count = 42u;
    original.snapshot_log_record_count = 7u;
    original.ble_connected = 1u;
    original.ble_tx_power_dbm = 4u;
    original.ble_peer_rssi_neg = 60u;
    original.cpu_temperature_001c = 4200u;
    original.post_last_status = 0u;
    original.uptime_seconds = 12345u;
    original.uptime_seconds_low = static_cast<uint8_t>(12345u & 0xFFu);
    original.trailer_magic = SNAPSHOT_TRAILER_MAGIC;

    uint8_t buf[sizeof(device_snapshot)];
    std::memcpy(buf, &original, sizeof(original));

    auto restored = device_snapshot{};
    std::memcpy(&restored, buf, sizeof(restored));

    if (std::memcmp(&original, &restored, sizeof(original)) != 0) {
        *why = "round-trip mismatch";
        return false;
    }
    return true;
}

bool body_forward_compat_probe(const char **why) {
    // A snapshot from a *newer* firmware (unknown version) must still expose its
    // fixed-offset header fields to an older decoder.
    auto s = device_snapshot{};
    std::memset(&s, 0, sizeof(s));
    s.unix_timestamp = 1700000000u;
    s.snapshot_version = 99u; // an unrecognized future version
    s.trailer_magic = SNAPSHOT_TRAILER_MAGIC;

    uint8_t buf[sizeof(device_snapshot)];
    std::memcpy(buf, &s, sizeof(s));

    const auto ver = buf[offsetof(device_snapshot, snapshot_version)];
    auto ts = uint32_t{};
    std::memcpy(&ts, buf + offsetof(device_snapshot, unix_timestamp),
                sizeof(ts));

    if (ver != 99u) {
        *why = "version unreadable";
        return false;
    }
    if (ts != 1700000000u) {
        *why = "timestamp unreadable under unknown version";
        return false;
    }
    return true;
}

bool body_trailer_magic(const char **why) {
    // A zero-filled (never-written / torn) record must NOT look valid.
    auto zeroed = device_snapshot{};
    std::memset(&zeroed, 0, sizeof(zeroed));
    if (zeroed.trailer_magic == SNAPSHOT_TRAILER_MAGIC) {
        *why = "zeroed record reads as valid";
        return false;
    }

    auto valid = device_snapshot{};
    std::memset(&valid, 0, sizeof(valid));
    valid.trailer_magic = SNAPSHOT_TRAILER_MAGIC;
    if (valid.trailer_magic != SNAPSHOT_TRAILER_MAGIC) {
        *why = "valid magic not set";
        return false;
    }
    return true;
}

bool run_one(const char *name, bool (*body)(const char **)) noexcept {
    const char *why = "assertion";
    const auto  ok  = body(&why);
    report(name, ok, why);
    return ok;
}

} // namespace

// ============================================================================
// sentinel::test::device_snapshot::run_all
// ============================================================================

sentinel::test::tally sentinel::test::device_snapshot::run_all() noexcept {
    auto t = sentinel::test::tally{};

    t.record(run_one("size_invariant", body_size_invariant));
    yield_for_debug_drain(200);

    t.record(run_one("byte_layout", body_byte_layout));
    yield_for_debug_drain(200);

    t.record(run_one("round_trip", body_round_trip));
    yield_for_debug_drain(200);

    t.record(run_one("forward_compat_probe", body_forward_compat_probe));
    yield_for_debug_drain(200);

    t.record(run_one("trailer_magic", body_trailer_magic));
    yield_for_debug_drain(200);

    return t;
}
