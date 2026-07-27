///
/// \file    sentinel_gatt_ds3231.hpp
/// \brief   Accessor layer for the chip-named \c DS3231 GATT service (#6)
///
/// \details The DS3231's independent registers map to separate characteristics
///          (decision #9): Unix Time (R/W), RTC Temperature (R/Notify), Alarm
///          Flags (R). \c sentinel::gatt::ds3231 refreshes their values and
///          notifies subscribers, keeping the generated \c app_ds3231_* /
///          \c HDLC_DS3231_* symbols out of the producer/handler call sites.
///          \c inline + \c noexcept (the values are \c extern GATT-DB globals).
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0
///

#ifndef SENTINEL_GATT_DS3231_HPP
#define SENTINEL_GATT_DS3231_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cycfg_gatt_db.h"

#include "wiced_bt_gatt.h"
}
#pragma GCC diagnostic pop

#include "sentinel_ble_context.hpp"
#include "sentinel_ble_gatt.hpp"

#include <cstdint>

namespace sentinel::gatt::ds3231 {

// ---- Unix Time (R/W) ------------------------------------------------------

/// \brief Current Unix Time characteristic value (little-endian seconds).
/// \return Unix time in seconds.
inline uint32_t unix_time() noexcept {
    return static_cast<uint32_t>(app_ds3231_unix_time[0]) |
           (static_cast<uint32_t>(app_ds3231_unix_time[1]) << 8) |
           (static_cast<uint32_t>(app_ds3231_unix_time[2]) << 16) |
           (static_cast<uint32_t>(app_ds3231_unix_time[3]) << 24);
}

/// \brief Write the Unix Time characteristic (little-endian seconds).
/// \param unix_seconds New Unix time in seconds.
inline void set_unix_time(uint32_t unix_seconds) noexcept {
    uint8_t le[4] = {static_cast<uint8_t>(unix_seconds & 0xFFu),
                     static_cast<uint8_t>((unix_seconds >> 8) & 0xFFu),
                     static_cast<uint8_t>((unix_seconds >> 16) & 0xFFu),
                     static_cast<uint8_t>((unix_seconds >> 24) & 0xFFu)};
    ble_gatt_db_set_value(HDLC_DS3231_UNIX_TIME_VALUE, le,
                          static_cast<uint16_t>(sizeof(le)));
}

// ---- RTC Temperature (R/Notify) -------------------------------------------

/// \brief \c true while a central has subscribed to RTC Temperature
/// notifications.
/// \return \c true if the CCCD notification bit is set.
inline bool temperature_notifications_enabled() noexcept {
    return app_ds3231_rtc_temperature_client_char_config[0] &
           wiced_bt_gatt_client_char_config_e::GATT_CLIENT_CONFIG_NOTIFICATION;
}

/// \brief Write the RTC Temperature characteristic (0.01 °C / LSB,
/// little-endian).
/// \param centi_c Temperature in hundredths of a degree Celsius.
inline void set_temperature_centi_c(int16_t centi_c) noexcept {
    uint8_t le[2] = {
        static_cast<uint8_t>(static_cast<uint16_t>(centi_c) & 0xFFu),
        static_cast<uint8_t>((static_cast<uint16_t>(centi_c) >> 8) & 0xFFu)};
    ble_gatt_db_set_value(HDLC_DS3231_RTC_TEMPERATURE_VALUE, le,
                          static_cast<uint16_t>(sizeof(le)));
}

/// \brief Notify the connected central with the current RTC Temperature value.
/// \param connection_id BLE connection to notify.
/// \return \c wiced_bt_gatt_status_t result of the notification send.
inline wiced_bt_gatt_status_t
notify_temperature(uint16_t connection_id) noexcept {
    return wiced_bt_gatt_server_send_notification(
        connection_id, HDLC_DS3231_RTC_TEMPERATURE_VALUE,
        app_ds3231_rtc_temperature_len, app_ds3231_rtc_temperature, nullptr);
}

// ---- Alarm Flags (R) ------------------------------------------------------

/// \brief Write the Alarm Flags characteristic (bit0 A1F, bit1 A2F, bit7 OSF).
/// \param flags New alarm-flags byte.
inline void set_alarm_flags(uint8_t flags) noexcept {
    app_ds3231_alarm_flags[0] = flags;
}

///
/// \brief Publish RTC Temperature: refresh the read value and notify a
///        subscribed central. Unix Time is refreshed separately via
///        \ref sentinel::gatt::ds3231::set_unix_time on its own (1 Hz) cadence
///        — it does not notify.
///
/// \param temperature_centi_c Temperature in hundredths of a degree Celsius.
///
inline void publish_temperature(int16_t temperature_centi_c) noexcept {
    set_temperature_centi_c(temperature_centi_c);
    if (sentinel::ble_context_object.connected() &&
        temperature_notifications_enabled()) {
        notify_temperature(sentinel::ble_context_object.connection_id());
    }
}

} // namespace sentinel::gatt::ds3231

#endif /* SENTINEL_GATT_DS3231_HPP */
