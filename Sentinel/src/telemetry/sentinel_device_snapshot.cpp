///
/// \file    sentinel_device_snapshot.cpp
/// \brief   populate_snapshot — aggregate live device state into a snapshot (#36)
///
/// \details Implements \ref sentinel::telemetry::populate_snapshot. Per decision
///          #14, it aggregates from already-cached subsystem state and issues no
///          fresh bus transaction, so the live stream (#46) can call it every
///          ~100 ms with zero I²C/SPI contention. Fields whose producer is not
///          yet wired (storage counts, BLE state, CPU temp, POST status) are
///          left zero — they land with the boot orchestrator + shared device
///          context (#38) and the GATT layer (#6). \c trailer_magic is written
///          last so a half-built record is detectable.
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - Initial populate_snapshot implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_device_snapshot.hpp"
#include "sentinel_firmware_version.hpp"
#include "sentinel_task_bme280_service.hpp"
#include "sentinel_task_rtc_service.hpp"

#include <cstdint>
#include <cstring>

namespace sentinel::telemetry {

void populate_snapshot(device_snapshot *out) noexcept {
    if (out == nullptr) {
        return;
    }

    // Zero-init: every field defaults to its documented "absent" sentinel (0),
    // so a subsystem we cannot source simply leaves its bytes clear.
    std::memset(out, 0, sizeof(*out));

    // ---- Header ----
    out->unix_timestamp = sentinel::task::rtc_service::instance().last_unix_time();
    out->snapshot_version = SNAPSHOT_VERSION;
    out->firmware_major = sentinel::current_firmware_version.major();
    out->firmware_minor = sentinel::current_firmware_version.minor();
    out->firmware_patch = sentinel::current_firmware_version.patch();
    out->firmware_build = sentinel::current_firmware_version.build();

    // ---- Environmental — BME280 sample cache (#37) ----
    if (auto s = sentinel::task::bme280_service::instance().latest(); s.valid) {
        out->temperature_001c = s.temperature_centi_c;
        out->humidity_001pc = s.humidity_centi_pct;
        out->pressure_pa = s.pressure_pa;
    }

    // ---- Timekeeping — rtc_service caches ----
    out->rtc_temperature_001c =
        sentinel::task::rtc_service::instance().last_temperature_centi_c();
    // rtc_alarm_flags stays 0: Phase I configures no DS3231 alarms. Wire from a
    // status-register cache when the alarm subsystem is used.

    // ---- Storage: 0 until the app record stores exist (boot orchestrator, #38).
    // ---- BLE: 0 until ble_context publishes cached connection state (#29 / #6).

    // ---- System health ----
    out->uptime_seconds = static_cast<uint32_t>(xTaskGetTickCount()) /
                          static_cast<uint32_t>(configTICK_RATE_HZ);
    out->uptime_seconds_low = static_cast<uint8_t>(out->uptime_seconds & 0xFFu);
    // cpu_temperature_001c, post_last_status: 0 until wired (#38 orchestrator).

    // ---- Trailer (written last) ----
    out->trailer_magic = SNAPSHOT_TRAILER_MAGIC;
}

} // namespace sentinel::telemetry
