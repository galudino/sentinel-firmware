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
static inline uint8_t level() { return app_bas_battery_level[0]; }

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
static inline void set_level(uint8_t level) {
    app_bas_battery_level[0] = level;
}

} // namespace sentinel::gatt::battery

#endif /* SENTINEL_GATT_BATTERY_HPP */
