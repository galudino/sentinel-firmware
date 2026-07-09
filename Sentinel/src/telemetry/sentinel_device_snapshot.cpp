///
/// \file    sentinel_device_snapshot.cpp
/// \brief   populate_snapshot — aggregate live device state into a snapshot
/// (#36)
///
/// \details Implements \ref sentinel::telemetry::populate_snapshot. Per
/// decision
///          #14, it aggregates from already-cached subsystem state and issues
///          no fresh bus transaction, so the live stream (#46) can call it
///          every ~100 ms with zero I²C/SPI contention. Fields whose producer
///          is not yet wired (storage counts, BLE state, CPU temp, POST status)
///          are left zero — they land with the boot orchestrator + shared
///          device context (#38) and the GATT layer (#6). \c trailer_magic is
///          written last so a half-built record is detectable.
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

#include "sentinel_ble_context.hpp"
#include "sentinel_device_context.hpp"
#include "sentinel_device_snapshot.hpp"
#include "sentinel_firmware_version.hpp"
#include "sentinel_task_bme280_service.hpp"
#include "sentinel_task_rtc_service.hpp"

#include <cstdint>
#include <cstring>

namespace sentinel::telemetry {

void device_snapshot::populate(device_snapshot &out) noexcept {
    // Zero-init: every field defaults to its documented "absent" sentinel (0),
    // so a subsystem we cannot source simply leaves its bytes clear.
    std::memset(&out, 0, sizeof(out));

    // ---- Header ----
    out.unix_timestamp =
        sentinel::task::rtc_service::instance().last_unix_time();
    out.snapshot_version = SNAPSHOT_VERSION;
    out.firmware_major = sentinel::current_firmware_version.major();
    out.firmware_minor = sentinel::current_firmware_version.minor();
    out.firmware_patch = sentinel::current_firmware_version.patch();
    out.firmware_build = sentinel::current_firmware_version.build();

    // ---- Environmental — BME280 sample cache (#37) ----
    if (auto s = sentinel::task::bme280_service::instance().latest(); s.valid) {
        out.temperature_001c = s.temperature_centi_c;
        out.humidity_001pc = s.humidity_centi_pct;
        out.pressure_pa = s.pressure_pa;
    }

    // ---- Timekeeping — rtc_service caches ----
    out.rtc_temperature_001c =
        sentinel::task::rtc_service::instance().last_temperature_centi_c();
    // rtc_alarm_flags stays 0: Phase I configures no DS3231 alarms. Wire from a
    // status-register cache when the alarm subsystem is used.

    // ---- Storage + POST — shared device context (#38) ----
    // Only once the boot orchestrator has built the context and scanned the
    // flash regions; before then the counts are not yet meaningful and stay 0
    // (their documented sentinel). count() is head-tail arithmetic — no bus
    // I/O.
    if (sentinel::resource::context_ready()) {
        auto &ctx = sentinel::resource::context();
        out.event_log_record_count = ctx.event_log_record_count();
        out.snapshot_log_record_count = ctx.snapshot_record_count();
        out.post_last_status = ctx.post_last_status;
    }

    // ---- BLE — live connection state (#29) + link metrics (#6). ----
    out.ble_connected =
        sentinel::ble_context_object.connected() ? uint8_t{1} : uint8_t{0};
    if (out.ble_connected) {
        // Read the previously cached RSSI / TX power, then kick a fresh
        // (non-blocking, ~1 Hz throttled) read for the next snapshot. RSSI is
        // stored as its magnitude (60 => -60 dBm); TX power is a signed dBm
        // value carried in the uint8 field (decoded as int8 by the client).
        sentinel::ble_context_object.refresh_link_metrics();
        const auto rssi = sentinel::ble_context_object.peer_rssi();
        out.ble_peer_rssi_neg =
            static_cast<uint8_t>(rssi < 0 ? -static_cast<int>(rssi) : 0);
        out.ble_tx_power_dbm =
            static_cast<uint8_t>(sentinel::ble_context_object.tx_power_dbm());
    }

    // ---- System health ----
    out.uptime_seconds = static_cast<uint32_t>(xTaskGetTickCount()) /
                         static_cast<uint32_t>(configTICK_RATE_HZ);
    out.uptime_seconds_low = static_cast<uint8_t>(out.uptime_seconds & 0xFFu);
    // cpu_temperature_001c stays 0: reading the PSoC 6 on-die temperature needs
    // a SAR ADC internal-sensor channel + SFLASH calibration conversion (its own
    // small driver) — deferred follow-up; the field remains its documented 0
    // sentinel until then.

    // ---- Trailer (written last) ----
    out.trailer_magic = SNAPSHOT_TRAILER_MAGIC;
}

} // namespace sentinel::telemetry
