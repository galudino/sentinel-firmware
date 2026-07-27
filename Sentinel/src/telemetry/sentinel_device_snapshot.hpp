///
/// \file    sentinel_device_snapshot.hpp
/// \brief   device_snapshot — canonical aggregate of live device state (#36)
///
/// \details Defines the packed, fixed-point `device_snapshot` record: a
///          single-instant view of the whole device's live state. Two
///          independent consumers carry it (decision #14, two-lane model):
///            - the live BLE snapshot stream task (#46, lane 2) — notified at
///              ~100 ms while a capture session is active;
///            - the periodic snapshot persistence task (#38, lane 1) — written
///              to flash every ~5 min for historical retrieval.
///          Both call \ref sentinel::telemetry::device_snapshot::populate,
///          which aggregates from already- cached subsystem state (no fresh bus
///          I/O on the populate path), so lane 2 can stream at 100 ms with zero
///          I²C/SPI contention.
///
///          \b Wire \b contract. The layout is `__attribute__((packed))` so the
///          on-wire (BLE) and on-flash representation is byte-exact regardless
///          of compiler/platform. All multi-byte integers are little-endian
///          (PSoC 6 native; matches the iOS client decoder, #10). All physical
///          quantities are fixed-point integers with documented LSB units — no
///          host float-encoding ambiguity. The struct is **exactly 80 bytes**;
///          the `static_assert`s below lock both the total size and every field
///          offset. Never reorder existing fields or repurpose a field without
///          bumping \ref sentinel::telemetry::SNAPSHOT_VERSION; new fields
///          consume `reserved_*`
///          bytes (additive, forward-compatible).
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - Initial device_snapshot definition
///

#ifndef SENTINEL_DEVICE_SNAPSHOT_HPP
#define SENTINEL_DEVICE_SNAPSHOT_HPP

#include <cstddef>
#include <cstdint>

namespace sentinel::telemetry {

///
/// \brief Layout version for \ref sentinel::telemetry::device_snapshot. Bump on
///        any field
///        reorder/repurpose; additive field growth into `reserved_*` does not
///        require a bump (old decoders ignore unknown trailing fields).
///
inline constexpr uint8_t SNAPSHOT_VERSION = 1;

///
/// \brief Sentinel written last on a valid snapshot. A buffer/flash slot whose
///        final two bytes are not this value is a torn or never-written record.
///
inline constexpr uint16_t SNAPSHOT_TRAILER_MAGIC = 0xA5C3;

///
/// \brief Single-instant aggregate of live device state — 80 bytes, packed,
///        little-endian, fixed-point. See file header for the wire contract.
///
struct __attribute__((packed)) device_snapshot {
    // ---- Header (16 B) — fixed across all snapshots
    // --------------------------
    uint32_t unix_timestamp; ///< Seconds since 1970 (DS3231 at populate time; 0
                             ///< if RTC not yet ticked).
    uint8_t snapshot_version;   ///< \ref SNAPSHOT_VERSION at populate time.
    uint8_t firmware_major;     ///< Firmware version major.
    uint8_t firmware_minor;     ///< Firmware version minor.
    uint8_t firmware_patch;     ///< Firmware version patch.
    uint16_t firmware_build;    ///< Firmware version build.
    uint8_t reserved_header[6]; ///< Reserved; zero-filled.

    // ---- Environmental (8 B) — BME280 (#14, via #37 cache)
    // -------------------
    int16_t temperature_001c; ///< 0.01 °C / LSB (e.g. 2345 = 23.45 °C).
    uint16_t humidity_001pc;  ///< 0.01 %RH / LSB.
    uint32_t pressure_pa;     ///< Pascals.

    // ---- Timekeeping (8 B) — DS3231 (#15, via rtc_service cache)
    // -------------
    int16_t
        rtc_temperature_001c; ///< 0.01 °C / LSB from the RTC's onboard sensor.
    uint8_t rtc_alarm_flags;  ///< bit0 = A1F, bit1 = A2F, bit7 = OSF.
    uint8_t reserved_rtc[5];  ///< Reserved; zero-filled.

    // ---- Storage (16 B) — record stores (#33/#34/#38)
    // ------------------------
    uint32_t event_log_record_count; ///< Stored System Event Log records.
    uint32_t
        snapshot_log_record_count; ///< Stored device_snapshot history records.
    uint8_t reserved_storage[8];   ///< Reserved; zero-filled.

    // ---- BLE (8 B) — ble_context (#29)
    // ---------------------------------------
    uint8_t ble_connected;    ///< 0 or 1.
    uint8_t ble_tx_power_dbm; ///< Configured TX power, dBm.
    uint8_t
        ble_peer_rssi_neg; ///< -RSSI in dBm (60 → -60 dBm); 0 if not connected.
    uint8_t reserved_ble[5]; ///< Reserved; zero-filled.

    // ---- System health (16 B)
    // ------------------------------------------------
    uint16_t cpu_temperature_001c; ///< 0.01 °C / LSB; 0 until wired up.
    uint8_t
        post_last_status; ///< 0 = pass, else subsystem ID of first POST fail.
    uint8_t uptime_seconds_low; ///< LSB of \ref uptime_seconds, for cheap
                                ///< change-detection.
    uint32_t uptime_seconds;    ///< Seconds since boot.
    uint8_t reserved_health[8]; ///< Reserved; zero-filled.

    // ---- Trailer (8 B) — magic is the LAST field written
    // ---------------------
    uint8_t reserved_trailer[6]; ///< Reserved; zero-filled.
    uint16_t trailer_magic; ///< \ref SNAPSHOT_TRAILER_MAGIC on a valid record
                            ///< (written last).
    ///
    /// \brief Fill \p out with a fresh snapshot of current device state.
    ///
    /// \details Aggregates from already-cached subsystem state — the BME280
    /// sample
    ///          cache (#37), the rtc_service time/temperature cache, FreeRTOS
    ///          uptime — and never issues a fresh bus transaction (decision
    ///          #14). Zero-initializes first, then writes each field it can
    ///          source; a subsystem whose cache is empty/invalid leaves its
    ///          fields at 0 (a documented per-field sentinel) and the snapshot
    ///          still completes — partial data beats no data. \ref
    ///          device_snapshot::trailer_magic is written last.
    ///
    /// \param[out] out Destination snapshot. Must be non-null.
    ///
    static void populate(device_snapshot &out) noexcept;

    ///
    /// \brief Make a fresh snapshot of current device state.
    ///
    /// \details Calls \ref populate() to fill a local snapshot and returns it.
    ///
    ///          The returned snapshot is a copy of the local; the caller may
    ///          modify it freely. The local snapshot is destroyed on return.
    ///
    /// \return A fresh snapshot of current device state.
    ///
    static device_snapshot make() noexcept {
        device_snapshot snapshot;
        populate(snapshot);
        return snapshot;
    }
};

// --- Size + layout lock (the wire contract). -------------------------------
static_assert(sizeof(device_snapshot) == 80,
              "device_snapshot must be 80 bytes");
static_assert(sizeof(device_snapshot) % 16 == 0,
              "device_snapshot must be 16-byte aligned in size");

static_assert(offsetof(device_snapshot, unix_timestamp) == 0, "");
static_assert(offsetof(device_snapshot, snapshot_version) == 4, "");
static_assert(offsetof(device_snapshot, firmware_build) == 8, "");
static_assert(offsetof(device_snapshot, temperature_001c) == 16, "");
static_assert(offsetof(device_snapshot, humidity_001pc) == 18, "");
static_assert(offsetof(device_snapshot, pressure_pa) == 20, "");
static_assert(offsetof(device_snapshot, rtc_temperature_001c) == 24, "");
static_assert(offsetof(device_snapshot, rtc_alarm_flags) == 26, "");
static_assert(offsetof(device_snapshot, event_log_record_count) == 32, "");
static_assert(offsetof(device_snapshot, snapshot_log_record_count) == 36, "");
static_assert(offsetof(device_snapshot, ble_connected) == 48, "");
static_assert(offsetof(device_snapshot, cpu_temperature_001c) == 56, "");
static_assert(offsetof(device_snapshot, uptime_seconds) == 60, "");
static_assert(offsetof(device_snapshot, trailer_magic) == 78, "");

} // namespace sentinel::telemetry

#endif /* SENTINEL_DEVICE_SNAPSHOT_HPP */
