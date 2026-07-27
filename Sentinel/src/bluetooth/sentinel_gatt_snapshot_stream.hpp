///
/// \file    sentinel_gatt_snapshot_stream.hpp
/// \brief   Accessor layer for the \c Snapshot Stream GATT service (#6, lane 2)
///
/// \details Bridges the live \ref sentinel::task::snapshot_stream_task (#46,
///          lane 2) to GATT. \ref sentinel::gatt::snapshot_stream::notify_sink
///          is the \c notify_fn the task calls with each produced
///          \c sentinel::telemetry::device_snapshot; it
///          writes the 80-byte record to the Current Device Snapshot
///          characteristic and notifies a subscribed central. The Snapshot
///          Notify Enable characteristic drives the task's \c start() / \c
///          stop().
///          \c inline + \c noexcept over the \c extern GATT-DB globals.
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0
///

#ifndef SENTINEL_GATT_SNAPSHOT_STREAM_HPP
#define SENTINEL_GATT_SNAPSHOT_STREAM_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cycfg_gatt_db.h"

#include "wiced_bt_gatt.h"
}
#pragma GCC diagnostic pop

#include "sentinel_ble_context.hpp"
#include "sentinel_ble_gatt.hpp"
#include "sentinel_device_snapshot.hpp"

#include <cstdint>
#include <cstring>

namespace sentinel::gatt::snapshot_stream {

/// \brief \c true while a central has subscribed to Current Device Snapshot
///        notifications.
/// \return \c true if the CCCD notification bit is set.
inline bool notifications_enabled() noexcept {
    return app_snapshot_stream_current_device_snapshot_client_char_config[0] &
           wiced_bt_gatt_client_char_config_e::GATT_CLIENT_CONFIG_NOTIFICATION;
}

/// \brief Current value of the Snapshot Notify Enable characteristic (0/1).
/// \return \c true if the enable flag is set.
inline bool enable_flag() noexcept {
    return app_snapshot_stream_snapshot_notify_enable[0] != 0;
}

///
/// \brief \c notify_fn sink for \ref sentinel::task::snapshot_stream_task —
///        write the produced snapshot to GATT and notify a subscribed central.
///
/// \details Signature matches \c snapshot_stream_task::notify_fn. The 80-byte
///          \c device_snapshot is packed (wire contract, #36), so it is copied
///          verbatim into the characteristic value.
///
/// \param snap Snapshot record produced by the snapshot stream task.
///
inline void
notify_sink(const sentinel::telemetry::device_snapshot &snap) noexcept {
    ble_gatt_db_set_value(
        HDLC_SNAPSHOT_STREAM_CURRENT_DEVICE_SNAPSHOT_VALUE,
        reinterpret_cast<uint8_t *>(
            const_cast<sentinel::telemetry::device_snapshot *>(&snap)),
        static_cast<uint16_t>(sizeof(snap)));

    if (sentinel::ble_context_object.connected() && notifications_enabled()) {
        wiced_bt_gatt_server_send_notification(
            sentinel::ble_context_object.connection_id(),
            HDLC_SNAPSHOT_STREAM_CURRENT_DEVICE_SNAPSHOT_VALUE,
            app_snapshot_stream_current_device_snapshot_len,
            app_snapshot_stream_current_device_snapshot, nullptr);
    }
}

} // namespace sentinel::gatt::snapshot_stream

#endif /* SENTINEL_GATT_SNAPSHOT_STREAM_HPP */
