///
/// \file    sentinel_system_event.hpp
/// \brief   System Event Log record layout — typed-variant 36-byte records
///
/// \details Defines the on-storage record format for the System Event Log
///          (firmware #34): a closed \ref sentinel::diagnostics::system_event
///          enum, a common 8-byte
///          \ref sentinel::diagnostics::system_event_record_header, the 36-byte
///          untyped \ref sentinel::diagnostics::system_event_record that the
///          storage layer sees, and
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
///          \c RecordType. Mixing differently-sized records in one log is not
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
///          \ref sentinel::diagnostics::system_event_record view. Adding a
///          field to an existing typed view (eating reserved bytes) is
///          additive. *Re-purposing* an existing field is breaking — treat the
///          field layouts here as a wire contract shared with the BLE GATT
///          retrieval path (#6) and the iOS client, append-only and never
///          reused.
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
    boot_complete = 0x01,
    shutdown_clean = 0x02,
    shutdown_unexpected = 0x03, ///< Synthesized on next boot when the
                                ///< most recent record was not a clean
                                ///< shutdown.
    firmware_update_attempted = 0x04,
    firmware_update_completed = 0x05,
    firmware_update_failed = 0x06,
    firmware_reverted = 0x07,

    // POST results (0x10–0x1F) — see #35
    post_passed = 0x10,
    post_subsystem_failed = 0x11,

    // Configuration (0x20–0x2F)
    config_changed = 0x20,
    config_reset_to_default = 0x21,
    rtc_set_from_ble = 0x22,
    serial_number_set = 0x23,

    // BLE (0x30–0x3F)
    ble_stack_initialized = 0x30,
    ble_stack_deinitialized = 0x31,
    ble_peripheral_connected = 0x32,
    ble_peripheral_disconnected = 0x33,
    ble_paired = 0x34,
    ble_unpaired = 0x35,

    // Operational (0x40–0x4F)
    mode_changed = 0x40,
    fault_raised = 0x41,
    fault_cleared = 0x42,
    user_input = 0x43,
    sensor_threshold_crossed = 0x44,
    snapshot_persisted = 0x45, ///< Periodic snapshot heartbeat (#38): one
                               ///< every N flash captures, so the log has
                               ///< a "still alive" marker without flooding.
    pre_fault_snapshot_captured = 0x46, ///< A fault handler captured a snapshot
                                        ///< via capture_now() just before this;
                                        ///< correlate by adjacent timestamp.

    // Phase II-specific (0x50–0x6F) — fan / motor / thermal placeholders
    fan_rpm_threshold_changed = 0x50,
    fan_rpm_min_met = 0x51,
    fan_rpm_max_exceeded = 0x52,
    fan_temperature_threshold_changed = 0x53,
    fan_temperature_min_met = 0x54,
    fan_temperature_max_exceeded = 0x55,
    fan_power_consumption_exceeded = 0x56,

    // Reserved range for application-specific events
    application_defined_start = 0x80,
    application_defined_end = 0xFE,

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
    uint8_t major; ///< Major version number.
    uint8_t minor; ///< Minor version number.
    uint8_t patch; ///< Patch version number.
    uint8_t build; ///< Low 8 bits of the full build number.
};
static_assert(sizeof(firmware_version_compact) == 4);

///
/// \brief Pack a \ref sentinel::firmware_version into its compact record form.
///
/// \param v Full firmware version to pack.
/// \return The packed 4-byte \ref
/// sentinel::diagnostics::firmware_version_compact.
///
inline firmware_version_compact
to_compact(const sentinel::firmware_version &v) noexcept {
    return firmware_version_compact{v.major(), v.minor(), v.patch(),
                                    static_cast<uint8_t>(v.build())};
}

///
/// \brief High byte of a version's 16-bit build number (#86).
///
/// \details \ref sentinel::diagnostics::firmware_version_compact keeps only the
///          build's low byte, so
///          records carry this high byte in their (previously zeroed) reserved
///          tail. Records written before #86 read back \c 0 here, which
///          reassembles to exactly the old value.
///
/// \param v Full firmware version.
/// \return Bits 15..8 of \p v's build number.
///
inline uint8_t build_high_byte(const sentinel::firmware_version &v) noexcept {
    return static_cast<uint8_t>(v.build() >> 8u);
}

///
/// \brief Reassemble a full version from its compact form + build high byte
///        (#86).
///
/// \param c       Compact version (build low byte).
/// \param build_hi Build high byte, as stored alongside \p c.
/// \return The full four-part version, build = <tt>(build_hi << 8) |
/// c.build</tt>.
///
inline sentinel::firmware_version from_compact(firmware_version_compact c,
                                               uint8_t build_hi) noexcept {
    return sentinel::firmware_version(
        c.major, c.minor, c.patch,
        static_cast<uint16_t>((static_cast<uint16_t>(build_hi) << 8u) |
                              c.build));
}

///
/// \brief Resolve the post-swap event implied by a pending update marker (#81).
///
/// \details Called at boot when a durable pending-update marker is present
///          (see \ref sentinel::storage::firmware_update_marker). Compares the
///          version that started the update (\p from, carried across the reset
///          in the marker) against the version now running:
///          - a \e different image is running → the MCUBoot swap took effect →
///            \c firmware_update_completed;
///          - the \e same image is still running → no swap took effect (MCUBoot
///            reverted, or the reset happened before any swap) →
///            \c firmware_reverted.
///          All four components are compared at full width (#86 — the build
///          is 16-bit, so 1.0.2.111 and 1.0.2.367 are different images even
///          though their low build bytes match). Direction-agnostic, so a
///          downgrade is still a completed update.
///
/// \param from    Full version recorded when the update was attempted.
/// \param running Version of the image now executing.
/// \return \c firmware_update_completed or \c firmware_reverted.
///
inline system_event
resolve_post_swap_event(const sentinel::firmware_version &from,
                        const sentinel::firmware_version &running) noexcept {
    const bool same_image =
        from.major() == running.major() && from.minor() == running.minor() &&
        from.patch() == running.patch() && from.build() == running.build();
    return same_image ? system_event::firmware_reverted
                      : system_event::firmware_update_completed;
}

///
/// \brief Discrete reasons carried in \c firmware_update_record::mcuboot_result
///        for a \c firmware_update_failed event (#83/#84/#71); mirrored by the
///        client's Logs view.
///
namespace ota_fail_reason {
inline constexpr uint32_t aborted = 1u;     ///< Client sent the ABORT command.
inline constexpr uint32_t signal_lost = 2u; ///< Peer disconnected mid-OTA.
inline constexpr uint32_t verify = 3u;      ///< Image verification failed.
inline constexpr uint32_t start = 4u;       ///< OTA agent failed to start.
inline constexpr uint32_t interrupted = 5u; ///< Board reset mid-download
                                            ///< (power cut / crash), found at
                                            ///< the next boot (#71).
} // namespace ota_fail_reason

///
/// \brief Resolve a pending-update marker found at boot into its event (#71).
///
/// \details Extends \ref sentinel::diagnostics::resolve_post_swap_event with
/// the marker's download
///          stage. A different running version is always
///          \c firmware_update_completed. The same version means the update
///          did not take: \c firmware_reverted if the download had finished
///          (MCUBoot rejected or rolled back the image), but
///          \c firmware_update_failed if it had not — the reset (a power cut
///          or crash) interrupted the download itself, which is not a revert.
///
/// \param download_finished \c false only if the marker says the download was
///                          still in progress when the board reset.
/// \param from              Version running when the update was attempted.
/// \param running           Version running now.
/// \return \c firmware_update_completed, \c firmware_reverted, or
///         \c firmware_update_failed (reason \ref
///         sentinel::diagnostics::ota_fail_reason::interrupted).
///
inline system_event
resolve_pending_update(bool download_finished,
                       const sentinel::firmware_version &from,
                       const sentinel::firmware_version &running) noexcept {
    const auto step = resolve_post_swap_event(from, running);
    if (step == system_event::firmware_reverted && !download_finished) {
        return system_event::firmware_update_failed;
    }
    return step;
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
    uint32_t unix_timestamp; ///< bytes 0..3 — RTC reading at record time,
                             ///< or 0 if the RTC was not yet available.
    system_event event_type; ///< byte  4
    uint8_t reserved[3];     ///< bytes 5..7 — forward-compat.
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
    system_event_record_header header; ///< Common 8-byte header.
    uint8_t data[28];                  ///< Untyped body; reinterpret via a
                                       ///< typed view for \c header.event_type.
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
    system_event_record_header header;         ///< Common 8-byte header.
    firmware_version_compact firmware_version; ///< 4 bytes
    uint32_t boot_count;                       ///< total boots since erase
    uint32_t uptime_at_event;                  ///< seconds (0 for boot)
    uint8_t build_hi;     ///< Build high byte (#86); 0 on pre-#86 records.
    uint8_t reserved[15]; ///< Forward-compat padding.
};
static_assert(sizeof(boot_lifecycle_record) == 36);

///
/// \brief firmware_update_attempted / _completed / _failed / firmware_reverted.
///
struct firmware_update_record {
    system_event_record_header header;     ///< Common 8-byte header.
    firmware_version_compact from_version; ///< 4 bytes
    firmware_version_compact to_version;   ///< 4 bytes
    uint32_t mcuboot_result;               ///< last image-check / etc.
    uint8_t from_build_hi; ///< \c from build high byte (#86); 0 pre-#86.
    uint8_t to_build_hi;   ///< \c to build high byte (#86); 0 pre-#86.
    uint8_t reserved[14];  ///< Forward-compat padding.
};
static_assert(sizeof(firmware_update_record) == 36);

///
/// \brief ble_peripheral_connected / ble_peripheral_disconnected.
///
struct ble_connection_record {
    system_event_record_header header; ///< Common 8-byte header.
    uint8_t peer_address[6];           ///< BD_ADDR
    uint8_t peer_addr_type;            ///< Public/random address type.
    uint8_t disconnect_reason;         ///< 0 for connected events
    uint16_t conn_interval_125us; ///< Connection interval, units of 1.25 ms.
    uint16_t peer_mtu;            ///< Negotiated ATT MTU with the peer.
    uint8_t reserved[16];         ///< Forward-compat padding.
};
static_assert(sizeof(ble_connection_record) == 36);

///
/// \brief fault_raised / fault_cleared.
///
struct fault_record {
    system_event_record_header header; ///< Common 8-byte header.
    uint8_t fault_id;                  ///< Application-defined fault id.
    uint8_t severity;                  ///< 0=info 1=warn 2=error 3=crit
    uint8_t subsystem_id;              ///< Subsystem that raised the fault.
    uint8_t reserved_inline;           ///< Forward-compat padding.
    uint32_t fault_context[6];         ///< free-form per-fault context
};
static_assert(sizeof(fault_record) == 36);

///
/// \brief mode_changed.
///
struct mode_change_record {
    system_event_record_header header; ///< Common 8-byte header.
    uint8_t from_mode;                 ///< Mode transitioned from.
    uint8_t to_mode;                   ///< Mode transitioned to.
    uint8_t trigger;                   ///< ble | button | watchdog | post_fail
    uint8_t reserved_inline;           ///< Forward-compat padding.
    uint8_t reserved[24];              ///< Forward-compat padding.
};
static_assert(sizeof(mode_change_record) == 36);

///
/// \brief post_passed / post_subsystem_failed (see #35).
///
struct post_result_record {
    system_event_record_header header; ///< Common 8-byte header.
    uint8_t subsystem_id;              ///< 0 for post_passed
    uint8_t result;                    ///< per-subsystem result code
    uint8_t detail;                    ///< per-subsystem detail byte
    uint8_t reserved_inline;           ///< Forward-compat padding.
    uint8_t reserved[24];              ///< Forward-compat padding.
};
static_assert(sizeof(post_result_record) == 36);

///
/// \brief snapshot_persisted / pre_fault_snapshot_captured (#38).
///
/// \details Ties a System Event Log entry to the device-snapshot history store:
///          \c snapshot_count is the snapshot store's record count at the
///          moment the event was recorded, so a reader can jump from the log
///          heartbeat to the corresponding snapshot. \c reason distinguishes
///          the periodic heartbeat from a fault-handler capture.
///
struct snapshot_event_record {
    system_event_record_header header; ///< Common 8-byte header.
    uint32_t snapshot_count;           ///< Snapshot store count at event.
    uint8_t reason;                    ///< 0=periodic heartbeat, 1=pre-fault.
    uint8_t reserved[23];              ///< Forward-compat padding.
};
static_assert(sizeof(snapshot_event_record) == 36);

///
/// \brief fan_rpm_threshold_changed (Phase II).
///
struct fan_rpm_threshold_change_record {
    system_event_record_header header; ///< Common 8-byte header.
    uint16_t old_min_rpm;              ///< Previous minimum RPM threshold.
    uint16_t old_max_rpm;              ///< Previous maximum RPM threshold.
    uint16_t new_min_rpm;              ///< New minimum RPM threshold.
    uint16_t new_max_rpm;              ///< New maximum RPM threshold.
    uint8_t reserved[20];              ///< Forward-compat padding.
};
static_assert(sizeof(fan_rpm_threshold_change_record) == 36);

///
/// \brief Is \p e one of the boot-lifecycle events carried by
///        \ref sentinel::diagnostics::boot_lifecycle_record?
///
/// \details Used by the boot sequence to recover the running boot count from
///          the most recent lifecycle record.
///
/// \param e Event type to test.
/// \return \c true if \p e is \c boot_complete, \c shutdown_clean, or
///         \c shutdown_unexpected.
///
inline bool is_boot_lifecycle(system_event e) noexcept {
    return e == system_event::boot_complete ||
           e == system_event::shutdown_clean ||
           e == system_event::shutdown_unexpected;
}

} // namespace sentinel::diagnostics

#endif /* SENTINEL_SYSTEM_EVENT_HPP */
