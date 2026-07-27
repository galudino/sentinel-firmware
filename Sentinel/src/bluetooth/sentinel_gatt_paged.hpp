///
/// \file    sentinel_gatt_paged.hpp
/// \brief   Paged-read services: Snapshot History + System Event Log (#6)
///
/// \details Both aggregate services expose a flash-backed circular record store
///          (#33) over the same paged-read protocol: read Record Count, write
///          Index (a \e relative cursor in <tt>[0, count)</tt>), read Record
///          Block (\c floor(max/record_size) records from the cursor). The read
///          values are refreshed lazily from \c resource::context()'s stores in
///          \ref sentinel::gatt::paged::before_read, just before the GATT read
///          responds, so a client
///          always sees current data. Index writes are stored by the default
///          write path; Clear Store is deferred to the async maintenance task
///          (\c record_store::erase_all erases every sector — far too slow for
///          the BT callback).
///
///          The record stores use \b absolute indices in <tt>[tail, head)</tt>;
///          this maps the client's relative cursor via
///          <tt>tail_index() + cursor</tt>. Because the stores are circular the
///          tail advances on wrap — the per-record sequence numbers let the
///          client detect gaps (see #6).
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0
///

#ifndef SENTINEL_GATT_PAGED_HPP
#define SENTINEL_GATT_PAGED_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cycfg_gatt_db.h"

#include "wiced_bt_gatt.h"
}
#pragma GCC diagnostic pop

#include "sentinel_ble_gatt.hpp"
#include "sentinel_device_context.hpp"
#include "sentinel_device_snapshot.hpp"
#include "sentinel_system_event.hpp"

#include <cstdint>

namespace sentinel::gatt::paged {

/// \brief Largest Record Block payload (bounded by the GATT DB MaxAttrLength).
inline constexpr uint16_t BLOCK_SCRATCH = 512;

/// \brief Read a little-endian uint32 from a 4-byte GATT value array.
/// \param p Pointer to at least 4 bytes, little-endian encoded.
/// \return The decoded 32-bit value.
inline uint32_t read_u32_le(const uint8_t *p) noexcept {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

/// \brief Write a store's record count into its Record Count characteristic.
/// \tparam Store       Record-store type; must expose \c count().
/// \param  store       Record store to query.
/// \param  count_handle GATT-DB value handle of the Record Count characteristic.
template <typename Store>
inline void set_count(const Store &store, uint16_t count_handle) noexcept {
    const uint32_t c = store.count();
    uint8_t le[4] = {static_cast<uint8_t>(c & 0xFFu),
                     static_cast<uint8_t>((c >> 8) & 0xFFu),
                     static_cast<uint8_t>((c >> 16) & 0xFFu),
                     static_cast<uint8_t>((c >> 24) & 0xFFu)};
    ble_gatt_db_set_value(count_handle, le, static_cast<uint16_t>(sizeof(le)));
}

///
/// \brief Fill a Record Block characteristic with up to
///        \c floor(block_max/sizeof(Rec)) records starting at \p cursor
///        (relative index in <tt>[0, count)</tt>).
///
/// \tparam Rec         Record type stored in \p store.
/// \tparam Store       Record-store type; must expose \c count(),
///                     \c tail_index(), and \c read().
/// \param  store       Record store to read from.
/// \param  cursor      Relative starting index in <tt>[0, count)</tt>.
/// \param  block_handle GATT-DB value handle of the Record Block characteristic.
/// \param  block_max   Maximum bytes the characteristic can hold.
///
template <typename Rec, typename Store>
inline void fill_block(const Store &store, uint32_t cursor,
                       uint16_t block_handle, uint32_t block_max) noexcept {
    static uint8_t buf[BLOCK_SCRATCH]; // GATT reads are serialized in the BT task.

    const uint32_t rec = static_cast<uint32_t>(sizeof(Rec));
    const uint32_t per = block_max / rec;
    const uint32_t cnt = store.count();

    uint32_t n = (cursor < cnt) ? (cnt - cursor) : 0u;
    if (n > per) {
        n = per;
    }

    uint32_t bytes = 0;
    for (uint32_t i = 0; i < n; ++i) {
        auto *dst = reinterpret_cast<Rec *>(buf + bytes);
        if (!store.read(store.tail_index() + cursor + i, dst)) {
            break; // a torn/overwritten slot ends the block early.
        }
        bytes += rec;
    }
    ble_gatt_db_set_value(block_handle, buf, static_cast<uint16_t>(bytes));
}

/// \brief Refresh the Snapshot History Record Count (used after a clear).
inline void refresh_snapshot_count() noexcept {
    if (resource::context_ready()) {
        set_count(resource::context().snapshot_store,
                  HDLC_SNAPSHOT_HISTORY_RECORD_COUNT_VALUE);
    }
}

/// \brief Refresh the System Event Log Record Count (used after a clear).
inline void refresh_event_count() noexcept {
    if (resource::context_ready()) {
        set_count(resource::context().event_store,
                  HDLC_SYSTEM_EVENT_LOG_RECORD_COUNT_VALUE);
    }
}

///
/// \brief Refresh a paged characteristic value just before a GATT read responds.
///
/// \details Called at the top of the read handler; a no-op for non-paged
///          handles. Record Count is recomputed from the store; Record Block is
///          filled from the current Index cursor.
///
/// \param handle GATT-DB value handle about to be read.
///
inline void before_read(uint16_t handle) noexcept {
    if (!resource::context_ready()) {
        return;
    }
    auto &ctx = resource::context();

    switch (handle) {
    case HDLC_SNAPSHOT_HISTORY_RECORD_COUNT_VALUE:
        set_count(ctx.snapshot_store, handle);
        break;
    case HDLC_SNAPSHOT_HISTORY_RECORD_BLOCK_VALUE:
        fill_block<sentinel::telemetry::device_snapshot>(
            ctx.snapshot_store, read_u32_le(app_snapshot_history_index), handle,
            MAX_LEN_SNAPSHOT_HISTORY_RECORD_BLOCK);
        break;
    case HDLC_SYSTEM_EVENT_LOG_RECORD_COUNT_VALUE:
        set_count(ctx.event_store, handle);
        break;
    case HDLC_SYSTEM_EVENT_LOG_RECORD_BLOCK_VALUE:
        fill_block<sentinel::diagnostics::system_event_record>(
            ctx.event_store, read_u32_le(app_system_event_log_index), handle,
            MAX_LEN_SYSTEM_EVENT_LOG_RECORD_BLOCK);
        break;
    default:
        break;
    }
}

} // namespace sentinel::gatt::paged

#endif /* SENTINEL_GATT_PAGED_HPP */
