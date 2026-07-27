///
/// \file    sentinel_gatt_battery.hpp
/// \brief   Bluetooth LE public interface for characteristic values in the
/// battery service
///
/// \details This header provides the public interface for accessing and
/// updating
///          the characteristic values in the battery service, specifically the
///          battery level.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0
///

#ifndef SENTINEL_GATT_BATTERY_HPP
#define SENTINEL_GATT_BATTERY_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cycfg_gatt_db.h"

#include "wiced_bt_gatt.h"
}
#pragma GCC diagnostic pop

#include <cstdint>

namespace sentinel::gatt::battery {

// These accessors read/write the generated GATT-DB globals (defined in the
// generated cycfg_gatt_db.c), so they are `inline` — never `constexpr` — as
// their values are not constant expressions. This header is the single seam
// between portable firmware logic and the platform's generated symbols; call
// sites must not name `app_bas_*` / `HDLC_BAS_*` directly.

///
/// \brief Get current battery level percentage
///
/// Reads the current battery level percentage from the GATT database. The value
/// is stored in the app_bas_battery_level characteristic, which is updated by
/// the battery service task. The value is a single byte representing the
/// battery level percentage (0-100%).
///
/// \return uint8_t Current battery level percentage
///
/// Note: The battery level is simulated and updated periodically by the battery
/// service task.
///
inline uint8_t level() noexcept { return app_bas_battery_level[0]; }

///
/// \brief Get size of battery level characteristic
///
/// \return uint16_t Size of the battery level characteristic in bytes
///
inline uint16_t level_size() noexcept { return app_bas_battery_level_len; }

///
/// \brief Set battery level percentage
///
/// Updates the battery level percentage in the GATT database. This function is
/// used by the battery service task to simulate battery level changes. The
/// value is stored in the app_bas_battery_level characteristic, which is
/// exposed to BLE clients. The value should be a single byte representing the
/// battery level percentage (0-100%).
///
/// \param level New battery level percentage to set (0-100%)
///
inline void set_level(uint8_t level) noexcept {
    app_bas_battery_level[0] = level;
}

///
/// \brief Send a notification of the current battery level to \p connection_id.
///
/// \details Keeps the `HDLC_BAS_*` handle and value bytes inside this accessor
///          layer so the service task never names generated symbols directly.
///
/// \param connection_id BLE connection to notify.
///
/// \return wiced_bt_gatt_status_t result of the notification send.
///
inline wiced_bt_gatt_status_t notify(uint16_t connection_id) noexcept {
    return wiced_bt_gatt_server_send_notification(
        connection_id, HDLC_BAS_BATTERY_LEVEL_VALUE, app_bas_battery_level_len,
        app_bas_battery_level, nullptr);
}

} // namespace sentinel::gatt::battery

#endif /* SENTINEL_GATT_BATTERY_HPP */
