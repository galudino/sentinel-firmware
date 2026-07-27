///
/// \file    sentinel_system_event_log.hpp
/// \brief   System Event Log — typed event recorder over a record store
///
/// \details Implements the System Event Log (firmware #34): a durable,
///          store-backed log of discrete system events (boot, BLE, faults,
///          config, POST, firmware updates, …). Recording is non-blocking — a
///          \c record_*() call stamps a timestamp, packs a 36-byte
///          \ref sentinel::diagnostics::system_event_record, and pushes it onto
///          a FreeRTOS staging queue. A dedicated drain task pops the queue and
///          \c append()s to the backing store, keeping slow SPI flash traffic
///          off the caller's critical path.
///
///          === Templated on the store (deviation from the #34 sketch) ===
///
///          \ref sentinel::diagnostics::system_event_log is templated on its
///          \c Store type rather than bound to the flash
///          \ref sentinel::record_store. The same code runs
///          over the flash store in the application and over
///          \ref sentinel::ram_record_store in the testbench, with no virtual
///          dispatch. \c Store is duck-typed: it must provide
///          \c initialize/append/read/count/head_index/tail_index/capacity/
///          erase_all. The singleton accessor is therefore
///          \c system_event_log<Store>::instance() — one instance per store
///          type, which never collide because the app and tests use different
///          \c Store instantiations.
///
///          === Time source (deviation from the #34 sketch) ===
///
///          The log takes a \c now_unix_fn callback — "Unix seconds, or 0 if
///          unavailable" — instead of a \c ds3231& reference. This decouples
///          the log from the RTC driver and makes timestamps deterministic in
///          tests (the unexpected-shutdown synthesis depends on a controllable
///          clock). The application passes a thin wrapper over the RTC service;
///          tests pass a controllable function.
///
///          === Read concurrency ===
///
///          The drain task is the sole writer. It commits a record by writing
///          the payload and then advancing the store's head; an aligned 32-bit
///          head read is atomic on Cortex-M, so a concurrent reader (e.g. the
///          BLE retrieval path, #6) sees either the old head or a fully
///          committed new one — never a torn record. Reads therefore need no
///          lock. \ref sentinel::diagnostics::system_event_log::erase_all()
///          mutates head/tail and MUST NOT race the
///          drain task; callers gate it at the BLE layer and quiesce recording
///          first.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - System Event Log (firmware #34)
///

#ifndef SENTINEL_SYSTEM_EVENT_LOG_HPP
#define SENTINEL_SYSTEM_EVENT_LOG_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_firmware_version.hpp"
#include "sentinel_system_event.hpp"

#include <cstdint>
#include <cstring>

namespace sentinel::diagnostics {

///
/// \brief Application-facing flash event-log region (provisional).
///
/// \details The W25Q128 is 16 MiB. Issue #34 sizes the region at ~512 KiB:
///          the record store rounds a 36-byte record up to a 64-byte slot
///          (power-of-two), so 512 KiB holds 8,192 records — twice what a
///          256 KiB region would. The constant is consumed only by the (later)
///          application boot-wiring; the testbench validates against a RAM
///          store and ignores it. Finalise alongside the #38 snapshot region
///          so the two flash regions do not overlap.
///
inline constexpr uint32_t kEventLogRegionOffsetBytes = 0x100000u; // 1 MiB
/// Size of the event-log region in bytes (see \details above).
inline constexpr uint32_t kEventLogRegionSizeBytes   = 0x080000u; // 512 KiB

///
/// \brief Depth of the non-blocking staging queue (records).
///
inline constexpr uint32_t kStagingQueueDepth = 16u;

///
/// \brief Typed event recorder over a duck-typed record \p Store.
///
/// \tparam Store A record store exposing the \ref sentinel::record_store API.
///
template <typename Store>
class system_event_log {
public:
    /// "Unix seconds now, or 0 if the clock is not yet available."
    using now_unix_fn = uint32_t (*)();

    ///
    /// \brief Per-store-type singleton accessor.
    ///
    /// \return Reference to the process-wide instance for this \c Store type.
    ///
    static system_event_log &instance() noexcept {
        static system_event_log s_instance;
        return s_instance;
    }

    system_event_log(const system_event_log &)            = delete;
    system_event_log &operator=(const system_event_log &) = delete;

    ///
    /// \brief Bind the log to a store + clock and create the staging queue.
    ///
    /// \details Does not run the boot sequence and does not start the drain
    ///          task — call \ref run_boot_sequence() / \ref task_create() (app)
    ///          or drive \ref drain_pending() synchronously (tests). Safe to
    ///          call again to rebind to a fresh store (e.g. a simulated reboot
    ///          in tests); the existing queue is reset rather than leaked.
    ///
    /// \param store Backing store. Must already be \c initialize()d. Must
    ///              outlive the log.
    /// \param now   Clock callback (may be \c nullptr → timestamps are 0).
    /// \return \c true on success; \c false if the queue could not be created.
    ///
    bool initialize(Store &store, now_unix_fn now) noexcept {
        m_store = &store;
        m_now   = now;

        if (m_queue == nullptr) {
            m_queue = xQueueCreate(kStagingQueueDepth,
                                   sizeof(system_event_record));
            if (m_queue == nullptr) {
                m_initialized = false;
                return false;
            }
        } else {
            xQueueReset(m_queue);
        }

        m_initialized = true;
        return true;
    }

    // =====================================================================
    // Recording (non-blocking enqueue)
    // =====================================================================

    ///
    /// \brief Enqueue a fully-formed record. Stamps the timestamp for you.
    ///
    /// \details Non-blocking: pushes onto the staging queue with a zero
    ///          timeout and returns \c false if the queue is full (the caller
    ///          may choose to drop or retry). The drain task / \ref
    ///          drain_pending() later \c append()s it to the store.
    ///
    /// \param rec Fully-formed untyped record; its timestamp is overwritten.
    /// \return \c true if enqueued; \c false if not initialized or the
    ///         staging queue is full.
    bool record(const system_event_record &rec) noexcept {
        if (!m_initialized) {
            return false;
        }
        auto staged                  = rec;
        staged.header.unix_timestamp = current_time();
        return xQueueSend(m_queue, &staged, 0) == pdTRUE;
    }

    ///
    /// \brief Record a \c boot_complete event.
    ///
    /// \param v          Firmware version running this boot.
    /// \param boot_count Running boot count for this session.
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_boot_complete(const sentinel::firmware_version &v,
                              uint32_t boot_count) noexcept {
        auto r              = boot_lifecycle_record{};
        r.header.event_type = system_event::boot_complete;
        r.firmware_version  = to_compact(v);
        r.boot_count        = boot_count;
        r.uptime_at_event   = 0u;
        return record(as_record(r));
    }

    ///
    /// \brief Record a \c shutdown_clean event.
    ///
    /// \param v              Firmware version running this session.
    /// \param boot_count     Running boot count for this session.
    /// \param uptime_seconds Seconds since boot at shutdown time.
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_shutdown_clean(const sentinel::firmware_version &v,
                               uint32_t boot_count,
                               uint32_t uptime_seconds) noexcept {
        auto r              = boot_lifecycle_record{};
        r.header.event_type = system_event::shutdown_clean;
        r.firmware_version  = to_compact(v);
        r.boot_count        = boot_count;
        r.uptime_at_event   = uptime_seconds;
        return record(as_record(r));
    }

    ///
    /// \brief Record one step of a firmware-update lifecycle (attempted /
    ///        completed / failed / reverted).
    ///
    /// \param step           Which \ref system_event step this call represents.
    /// \param from           Version updating from.
    /// \param to             Version updating to.
    /// \param mcuboot_result Last MCUboot image-check / swap result code.
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_firmware_update(system_event step,
                                const sentinel::firmware_version &from,
                                const sentinel::firmware_version &to,
                                uint32_t mcuboot_result) noexcept {
        auto r              = firmware_update_record{};
        r.header.event_type = step;
        r.from_version      = to_compact(from);
        r.to_version        = to_compact(to);
        r.mcuboot_result    = mcuboot_result;
        return record(as_record(r));
    }

    ///
    /// \brief Record a \c ble_peripheral_connected event.
    ///
    /// \param peer_addr           Peer BD_ADDR (6 bytes).
    /// \param addr_type           Peer address type (public/random).
    /// \param conn_interval_125us Connection interval, units of 1.25 ms.
    /// \param peer_mtu            Negotiated ATT MTU with the peer.
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_ble_connected(const uint8_t peer_addr[6], uint8_t addr_type,
                              uint16_t conn_interval_125us,
                              uint16_t peer_mtu) noexcept {
        auto r              = ble_connection_record{};
        r.header.event_type = system_event::ble_peripheral_connected;
        std::memcpy(r.peer_address, peer_addr, sizeof(r.peer_address));
        r.peer_addr_type      = addr_type;
        r.disconnect_reason   = 0u;
        r.conn_interval_125us = conn_interval_125us;
        r.peer_mtu            = peer_mtu;
        return record(as_record(r));
    }

    ///
    /// \brief Record a \c ble_peripheral_disconnected event.
    ///
    /// \param peer_addr        Peer BD_ADDR (6 bytes).
    /// \param addr_type        Peer address type (public/random).
    /// \param disconnect_reason HCI/host disconnect reason code.
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_ble_disconnected(const uint8_t peer_addr[6], uint8_t addr_type,
                                 uint8_t disconnect_reason) noexcept {
        auto r              = ble_connection_record{};
        r.header.event_type = system_event::ble_peripheral_disconnected;
        std::memcpy(r.peer_address, peer_addr, sizeof(r.peer_address));
        r.peer_addr_type    = addr_type;
        r.disconnect_reason = disconnect_reason;
        return record(as_record(r));
    }

    ///
    /// \brief Record a \c fault_raised event.
    ///
    /// \param id        Application-defined fault id.
    /// \param severity  0=info 1=warn 2=error 3=crit.
    /// \param subsystem Subsystem that raised the fault.
    /// \param context   Optional free-form 6-word context (may be \c nullptr,
    ///                  in which case the record's context is left zeroed).
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_fault(uint8_t id, uint8_t severity, uint8_t subsystem,
                      const uint32_t context[6]) noexcept {
        auto r              = fault_record{};
        r.header.event_type = system_event::fault_raised;
        r.fault_id          = id;
        r.severity          = severity;
        r.subsystem_id      = subsystem;
        if (context != nullptr) {
            std::memcpy(r.fault_context, context, sizeof(r.fault_context));
        }
        return record(as_record(r));
    }

    ///
    /// \brief Record a \c mode_changed event.
    ///
    /// \param from    Mode transitioned from.
    /// \param to      Mode transitioned to.
    /// \param trigger ble | button | watchdog | post_fail.
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_mode_change(uint8_t from, uint8_t to, uint8_t trigger) noexcept {
        auto r              = mode_change_record{};
        r.header.event_type = system_event::mode_changed;
        r.from_mode         = from;
        r.to_mode           = to;
        r.trigger           = trigger;
        return record(as_record(r));
    }

    ///
    /// \brief Record a \c post_passed event (all POST subsystems passed).
    ///
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_post_passed() noexcept {
        auto r              = post_result_record{};
        r.header.event_type = system_event::post_passed;
        return record(as_record(r));
    }

    ///
    /// \brief Record a \c post_subsystem_failed event.
    ///
    /// \param subsystem Failed subsystem id (\ref post_subsystem).
    /// \param result    Per-subsystem result code (\ref post_result).
    /// \param detail    Per-subsystem detail byte.
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_post_subsystem_fail(uint8_t subsystem, uint8_t result,
                                    uint8_t detail) noexcept {
        auto r              = post_result_record{};
        r.header.event_type = system_event::post_subsystem_failed;
        r.subsystem_id      = subsystem;
        r.result            = result;
        r.detail            = detail;
        return record(as_record(r));
    }

    ///
    /// \brief Record a periodic snapshot-persistence heartbeat (#38, lane 1).
    ///
    /// \param snapshot_count Snapshot store count after the capture this marks.
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_snapshot_persisted(uint32_t snapshot_count) noexcept {
        auto r              = snapshot_event_record{};
        r.header.event_type = system_event::snapshot_persisted;
        r.snapshot_count    = snapshot_count;
        r.reason            = 0u; // periodic heartbeat
        return record(as_record(r));
    }

    ///
    /// \brief Record that a fault handler captured a snapshot (#38) — emit just
    ///        before the \c capture_now() append so the two correlate by time.
    ///
    /// \param snapshot_count Snapshot store count at capture time.
    /// \return \c true if enqueued; \c false otherwise (see \ref record()).
    ///
    bool record_pre_fault_snapshot(uint32_t snapshot_count) noexcept {
        auto r              = snapshot_event_record{};
        r.header.event_type = system_event::pre_fault_snapshot_captured;
        r.snapshot_count    = snapshot_count;
        r.reason            = 1u; // pre-fault capture
        return record(as_record(r));
    }

    // =====================================================================
    // Query (delegates to the store)
    // =====================================================================

    /// \brief Number of valid records currently stored.
    /// \return \c 0 if not yet bound to a store, else the store's \c count().
    uint32_t count() const noexcept {
        return m_store != nullptr ? m_store->count() : 0u;
    }

    /// \brief Read the record at an absolute index.
    /// \param index Absolute record index.
    /// \param out   Destination for the record.
    /// \return \c true on success; \c false if not bound to a store or the
    ///         store read failed.
    bool read(uint32_t index, system_event_record *out) const noexcept {
        return m_store != nullptr && m_store->read(index, out);
    }

    ///
    /// \brief Read \p n consecutive records starting at absolute \p start.
    ///
    /// \param start Absolute starting record index.
    /// \param n     Number of records to read.
    /// \param out   Destination array of at least \p n records.
    /// \return \c true on success; \c false if not bound to a store, \p out
    ///         is \c nullptr, or any underlying read failed.
    ///
    bool read_range(uint32_t start, uint32_t n,
                    system_event_record *out) const noexcept {
        if (m_store == nullptr || out == nullptr) {
            return false;
        }
        for (auto i = uint32_t{0}; i < n; i++) {
            if (!m_store->read(start + i, &out[i])) {
                return false;
            }
        }
        return true;
    }

    ///
    /// \brief Erase the entire log. Authentication is gated at the BLE layer.
    ///        MUST NOT race the drain task — quiesce recording first.
    ///
    /// \return \c true on success; \c false if not bound to a store or the
    ///         store erase failed.
    ///
    bool erase_all() noexcept {
        return m_store != nullptr && m_store->erase_all();
    }

    // =====================================================================
    // Boot sequence + draining
    // =====================================================================

    ///
    /// \brief Reconstruct lifecycle continuity on boot.
    ///
    /// \details If the most recent record is not a clean shutdown, synthesize a
    ///          \c shutdown_unexpected stamped with that record's timestamp
    ///          (the device's last known-alive instant). Then append a
    ///          \c boot_complete whose \c boot_count is one past the most recent
    ///          lifecycle record's count (or 1 on a fresh log). Both records are
    ///          appended directly to the store (bypassing the queue) so the
    ///          custom shutdown timestamp is preserved and the two records are
    ///          ordered ahead of any queued runtime events.
    ///
    /// \return \c true on success; \c false if not initialized, not bound to
    ///         a store, or an underlying store append failed.
    ///
    bool run_boot_sequence() noexcept {
        if (!m_initialized || m_store == nullptr) {
            return false;
        }

        const auto head = m_store->head_index();
        const auto tail = m_store->tail_index();

        auto last        = system_event_record{};
        auto have_last   = head > tail && m_store->read(head - 1u, &last);

        // Recover the running boot count from the most recent lifecycle record.
        auto prior_boot_count = uint32_t{0};
        auto found_lifecycle  = false;
        for (auto i = head; i > tail;) {
            --i;
            auto r = system_event_record{};
            if (!m_store->read(i, &r)) {
                continue;
            }
            if (is_boot_lifecycle(r.header.event_type)) {
                auto blr = boot_lifecycle_record{};
                std::memcpy(&blr, &r, sizeof(blr));
                prior_boot_count = blr.boot_count;
                found_lifecycle  = true;
                break;
            }
        }

        // Synthesize shutdown_unexpected if the last session did not end clean.
        if (have_last &&
            last.header.event_type != system_event::shutdown_clean) {
            auto su                  = boot_lifecycle_record{};
            su.header.event_type     = system_event::shutdown_unexpected;
            su.header.unix_timestamp = last.header.unix_timestamp;
            su.firmware_version      = to_compact(current_firmware_version);
            su.boot_count            = prior_boot_count;
            su.uptime_at_event       = 0u;
            if (!m_store->append(as_record(su))) {
                return false;
            }
        }

        // Append boot_complete for this session.
        auto bc                  = boot_lifecycle_record{};
        bc.header.event_type     = system_event::boot_complete;
        bc.header.unix_timestamp = current_time();
        bc.firmware_version      = to_compact(current_firmware_version);
        bc.boot_count           = found_lifecycle ? prior_boot_count + 1u : 1u;
        bc.uptime_at_event       = 0u;
        return m_store->append(as_record(bc));
    }

    ///
    /// \brief Drain every queued record into the store. Non-blocking.
    ///
    /// \details Used by the drain task loop and, in tests, called directly to
    ///          flush the queue deterministically. Returns the number of
    ///          records persisted.
    ///
    /// \return Number of records successfully appended to the store.
    ///
    uint32_t drain_pending() noexcept {
        if (!m_initialized || m_store == nullptr) {
            return 0u;
        }
        auto drained = uint32_t{0};
        auto rec     = system_event_record{};
        while (xQueueReceive(m_queue, &rec, 0) == pdTRUE) {
            if (m_store->append(rec)) {
                drained++;
            }
        }
        return drained;
    }

    ///
    /// \brief Drain-task body: run the boot sequence, then block-drain forever.
    ///
    void run() noexcept {
        run_boot_sequence();
        for (;;) {
            auto rec = system_event_record{};
            if (xQueueReceive(m_queue, &rec, portMAX_DELAY) == pdTRUE) {
                m_store->append(rec);
            }
        }
    }

    ///
    /// \brief Create and start the drain task. Application use.
    ///
    /// \return \c pdPASS on success; a FreeRTOS error code otherwise.
    ///
    BaseType_t task_create() noexcept {
        constexpr auto stack_words = configMINIMAL_STACK_SIZE * 4;
        constexpr auto priority =
            static_cast<UBaseType_t>(configMAX_PRIORITIES - 3);
        return xTaskCreate(
            [](void *) -> void { instance().run(); }, "System Event Log Task",
            stack_words, nullptr, priority, nullptr);
    }

private:
    system_event_log() = default;

    /// \brief Current Unix time via the bound clock callback.
    /// \return \c m_now() if bound; \c 0 otherwise.
    uint32_t current_time() const noexcept {
        return m_now != nullptr ? m_now() : 0u;
    }

    /// memcpy a typed view into the untyped record the store stores.
    /// \param t Typed record view to convert; must be exactly 36 bytes.
    /// \return The equivalent untyped \ref system_event_record.
    template <typename TypedRecord>
    static system_event_record as_record(const TypedRecord &t) noexcept {
        static_assert(sizeof(TypedRecord) == sizeof(system_event_record),
                      "typed views must be exactly 36 bytes");
        auto out = system_event_record{};
        std::memcpy(&out, &t, sizeof(out));
        return out;
    }

    Store        *m_store{nullptr};       ///< Non-owning backing store.
    now_unix_fn   m_now{nullptr};          ///< Clock callback, or \c nullptr.
    QueueHandle_t m_queue{nullptr};        ///< Non-blocking staging queue.
    bool          m_initialized{false};    ///< Set once \ref initialize() runs.
};

} // namespace sentinel::diagnostics

#endif /* SENTINEL_SYSTEM_EVENT_LOG_HPP */
