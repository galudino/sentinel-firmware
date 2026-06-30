///
/// \file    sentinel_system_event.hpp
/// \brief   System Event Log record layout — typed-variant 36-byte records
///
/// \details Defines the on-storage record format for the System Event Log
///          (firmware #34): a closed \ref sentinel::diagnostics::system_event
///          enum, a common 8-byte \ref system_event_record_header, the 36-byte
///          untyped \ref system_event_record that the storage layer sees, and
///          one typed *view* struct per event family. Every typed view is
///          exactly 36 bytes (`static_assert(sizeof(...) == 36)`), so any of
///          them can be \c memcpy-ed to/from the untyped record and stored by
///          \ref sentinel::record_store / \ref sentinel::ram_record_store.
///
///          === Why typed-variant 36-byte records (not uniform 8-byte) ===
///
///          Phase I events span a wide richness range. \c boot_complete needs
///          almost nothing; \c firmware_update_completed needs old + new
///          versions + a result code; \c fan_rpm_threshold_changed needs old +
///          new min/max. A uniform 3-byte payload would force the rich events
///          out-of-band and kill the log's "flight recorder" property. A 36-
///          byte record (8-byte header + 28-byte body) still yields ~7k records
///          per 256 KiB region — ample history — while keeping each event self-
///          describing. See issue #34 for the full rationale.
///
///          === The 36-byte invariant ===
///
///          The \ref sentinel::record_store template stores one fixed-size
///          \c RecordT. Mixing differently-sized records in one log is not
///          supported, so EVERY typed view MUST stay exactly 36 bytes. Adding a
///          field to a typed view means removing the same number of bytes from
///          its \c reserved padding. The per-view \c static_assert enforces it
///          at compile time. Field layouts are also chosen so the natural
///          alignment of each member is satisfied without hidden padding (the
///          header's leading \c uint32_t makes the whole record 4-aligned).
///
///          === Forward compatibility ===
///
///          Adding a new event type is additive: old firmware reading a newer
///          record sees an unknown enum value and falls back to the untyped
///          \ref system_event_record view. Adding a field to an existing typed
///          view (eating reserved bytes) is additive. *Re-purposing* an
///          existing field is breaking — treat the field layouts here as a wire
///          contract shared with the BLE GATT retrieval path (#6) and the iOS
///          client, append-only and never reused.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - System Event Log record layout (firmware #34)
///

#ifndef SENTINEL_SYSTEM_EVENT_HPP
#define SENTINEL_SYSTEM_EVENT_HPP

#include "sentinel_firmware_version.hpp"

#include <cstdint>
#include <type_traits>

namespace sentinel::diagnostics {

///
/// \brief Closed enumeration of every discrete system event.
///
/// \details Values are grouped into stable ranges by family so new events can
///          be added near their relatives without renumbering. The underlying
///          type is \c uint8_t so an event type costs one byte in the record
///          header. These values are a permanent wire contract (shared with
///          the BLE GATT log-retrieval path and the iOS client): append-only,
///          never reused, never renumbered.
///
enum class system_event : uint8_t {
    // Boot / lifecycle (0x01–0x0F)
    boot_complete             = 0x01,
    shutdown_clean            = 0x02,
    shutdown_unexpected       = 0x03, ///< Synthesized on next boot when the
                                      ///< most recent record was not a clean
                                      ///< shutdown.
    firmware_update_attempted = 0x04,
    firmware_update_completed = 0x05,
    firmware_update_failed    = 0x06,
    firmware_reverted         = 0x07,

    // POST results (0x10–0x1F) — see #35
    post_passed           = 0x10,
    post_subsystem_failed = 0x11,

    // Configuration (0x20–0x2F)
    config_changed          = 0x20,
    config_reset_to_default = 0x21,
    rtc_set_from_ble        = 0x22,
    serial_number_set       = 0x23,

    // BLE (0x30–0x3F)
    ble_stack_initialized    = 0x30,
    ble_stack_deinitialized  = 0x31,
    ble_peripheral_connected = 0x32,
    ble_peripheral_disconnected = 0x33,
    ble_paired   = 0x34,
    ble_unpaired = 0x35,

    // Operational (0x40–0x4F)
    mode_changed             = 0x40,
    fault_raised             = 0x41,
    fault_cleared            = 0x42,
    user_input               = 0x43,
    sensor_threshold_crossed = 0x44,
    snapshot_persisted       = 0x45, ///< Periodic snapshot heartbeat (#38): one
                                     ///< every N flash captures, so the log has
                                     ///< a "still alive" marker without flooding.
    pre_fault_snapshot_captured = 0x46, ///< A fault handler captured a snapshot
                                        ///< via capture_now() just before this;
                                        ///< correlate by adjacent timestamp.

    // Phase II-specific (0x50–0x6F) — fan / motor / thermal placeholders
    fan_rpm_threshold_changed         = 0x50,
    fan_rpm_min_met                   = 0x51,
    fan_rpm_max_exceeded              = 0x52,
    fan_temperature_threshold_changed = 0x53,
    fan_temperature_min_met           = 0x54,
    fan_temperature_max_exceeded      = 0x55,
    fan_power_consumption_exceeded    = 0x56,

    // Reserved range for application-specific events
    application_defined_start = 0x80,
    application_defined_end   = 0xFE,

    invalid = 0xFF,
};

///
/// \brief Compact 4-byte firmware version stored inside event records.
///
/// \details \ref sentinel::firmware_version carries a baked version *string*
///          and is far larger than four bytes, so it cannot live inside a
///          36-byte record. This packed form keeps the version identifiable
///          within the record's tight byte budget. \c build is the low 8 bits
///          of the full build number; the authoritative full build is reported
///          out-of-band via the Device Information Service. All members are
///          \c uint8_t so the struct is 4-aligned and introduces no padding.
///
struct firmware_version_compact {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    uint8_t build; ///< Low 8 bits of the full build number.
};
static_assert(sizeof(firmware_version_compact) == 4);

///
/// \brief Pack a \ref sentinel::firmware_version into its compact record form.
///
inline firmware_version_compact
to_compact(const sentinel::firmware_version &v) noexcept {
    return firmware_version_compact{v.major(), v.minor(), v.patch(),
                                    static_cast<uint8_t>(v.build())};
}

///
/// \brief Common 8-byte header prefixing every record.
///
/// \details The leading \c uint32_t makes the whole record 4-aligned. The
///          three reserved bytes are forward-compat space (e.g. a per-type
///          event subcode) and read back as whatever was written (zeroed by
///          the helpers in this module).
///
struct system_event_record_header {
    uint32_t     unix_timestamp; ///< bytes 0..3 — RTC reading at record time,
                                 ///< or 0 if the RTC was not yet available.
    system_event event_type;     ///< byte  4
    uint8_t      reserved[3];    ///< bytes 5..7 — forward-compat.
};
static_assert(sizeof(system_event_record_header) == 8);

///
/// \brief Untyped catch-all record — what the storage layer stores.
///
/// \details Typed views below \c memcpy to/from this struct. A reader that
///          encounters an unknown \ref system_event still gets the header and
///          the raw 28-byte body.
///
struct system_event_record {
    system_event_record_header header;
    uint8_t                    data[28];
};
static_assert(sizeof(system_event_record) == 36);
static_assert(std::is_trivially_copyable_v<system_event_record>);
static_assert(sizeof(system_event_record) % 4 == 0);

// ===========================================================================
// Typed views (each exactly 36 bytes)
// ===========================================================================

///
/// \brief boot_complete / shutdown_clean / shutdown_unexpected.
///
struct boot_lifecycle_record {
    system_event_record_header header;
    firmware_version_compact   firmware_version; ///< 4 bytes
    uint32_t                   boot_count;       ///< total boots since erase
    uint32_t                   uptime_at_event;  ///< seconds (0 for boot)
    uint8_t                    reserved[16];
};
static_assert(sizeof(boot_lifecycle_record) == 36);

///
/// \brief firmware_update_attempted / _completed / _failed / firmware_reverted.
///
struct firmware_update_record {
    system_event_record_header header;
    firmware_version_compact   from_version;   ///< 4 bytes
    firmware_version_compact   to_version;      ///< 4 bytes
    uint32_t                   mcuboot_result; ///< last image-check / etc.
    uint8_t                    reserved[16];
};
static_assert(sizeof(firmware_update_record) == 36);

///
/// \brief ble_peripheral_connected / ble_peripheral_disconnected.
///
struct ble_connection_record {
    system_event_record_header header;
    uint8_t                    peer_address[6]; ///< BD_ADDR
    uint8_t                    peer_addr_type;
    uint8_t  disconnect_reason;  ///< 0 for connected events
    uint16_t conn_interval_125us;
    uint16_t peer_mtu;
    uint8_t  reserved[16];
};
static_assert(sizeof(ble_connection_record) == 36);

///
/// \brief fault_raised / fault_cleared.
///
struct fault_record {
    system_event_record_header header;
    uint8_t                    fault_id;
    uint8_t                    severity;     ///< 0=info 1=warn 2=error 3=crit
    uint8_t                    subsystem_id;
    uint8_t                    reserved_inline;
    uint32_t                   fault_context[6]; ///< free-form per-fault context
};
static_assert(sizeof(fault_record) == 36);

///
/// \brief mode_changed.
///
struct mode_change_record {
    system_event_record_header header;
    uint8_t                    from_mode;
    uint8_t                    to_mode;
    uint8_t                    trigger; ///< ble | button | watchdog | post_fail
    uint8_t                    reserved_inline;
    uint8_t                    reserved[24];
};
static_assert(sizeof(mode_change_record) == 36);

///
/// \brief post_passed / post_subsystem_failed (see #35).
///
struct post_result_record {
    system_event_record_header header;
    uint8_t                    subsystem_id; ///< 0 for post_passed
    uint8_t                    result;       ///< per-subsystem result code
    uint8_t                    detail;       ///< per-subsystem detail byte
    uint8_t                    reserved_inline;
    uint8_t                    reserved[24];
};
static_assert(sizeof(post_result_record) == 36);

///
/// \brief snapshot_persisted / pre_fault_snapshot_captured (#38).
///
/// \details Ties a System Event Log entry to the device-snapshot history store:
///          \c snapshot_count is the snapshot store's record count at the moment
///          the event was recorded, so a reader can jump from the log heartbeat
///          to the corresponding snapshot. \c reason distinguishes the periodic
///          heartbeat from a fault-handler capture.
///
struct snapshot_event_record {
    system_event_record_header header;
    uint32_t                   snapshot_count; ///< Snapshot store count at event.
    uint8_t                    reason;         ///< 0=periodic heartbeat, 1=pre-fault.
    uint8_t                    reserved[23];
};
static_assert(sizeof(snapshot_event_record) == 36);

///
/// \brief fan_rpm_threshold_changed (Phase II).
///
struct fan_rpm_threshold_change_record {
    system_event_record_header header;
    uint16_t                   old_min_rpm;
    uint16_t                   old_max_rpm;
    uint16_t                   new_min_rpm;
    uint16_t                   new_max_rpm;
    uint8_t                    reserved[20];
};
static_assert(sizeof(fan_rpm_threshold_change_record) == 36);

///
/// \brief Is \p e one of the boot-lifecycle events carried by
///        \ref boot_lifecycle_record?
///
/// \details Used by the boot sequence to recover the running boot count from
///          the most recent lifecycle record.
///
inline bool is_boot_lifecycle(system_event e) noexcept {
    return e == system_event::boot_complete ||
           e == system_event::shutdown_clean ||
           e == system_event::shutdown_unexpected;
}

} // namespace sentinel::diagnostics

#endif /* SENTINEL_SYSTEM_EVENT_HPP */
