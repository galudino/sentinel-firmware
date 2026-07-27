///
/// \file    sentinel_gatt_bme280.hpp
/// \brief   Accessor layer for the chip-named \c BME280 GATT service (#6)
///
/// \details Exposes the BME280 one-shot combined sample as a single packed
///          8-byte characteristic (decision #9: one IC read -> one
///          characteristic). \c sentinel::gatt::bme280 packs a reading into the
///          \ref sentinel::gatt::bme280::bme280_sample wire struct, writes it
///          to the Ambient Sample
///          characteristic, and notifies a subscribed central — keeping the
///          generated \c app_bme280_* / \c HDLC_BME280_* symbols out of the
///          producer task. \c inline + \c noexcept (the values are \c extern
///          GATT-DB globals, not constant expressions).
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0
///

#ifndef SENTINEL_GATT_BME280_HPP
#define SENTINEL_GATT_BME280_HPP

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

namespace sentinel::gatt::bme280 {

///
/// \struct sentinel::gatt::bme280::bme280_sample
/// \brief BME280 Ambient Sample wire contract — little-endian, packed, 8 bytes
///        (#6). Mirrored 1:1 by the client (sentinel-client #9).
///
struct __attribute__((packed)) bme280_sample {
    int16_t temperature_centi_degC; ///< 0.01 °C / LSB.
    uint16_t humidity_centi_pct;    ///< 0.01 %RH / LSB.
    uint32_t pressure_pa;           ///< Pa / LSB.
};
static_assert(sizeof(bme280_sample) == 8, "bme280_sample must be 8 bytes");

/// \brief \c true while a central has subscribed to Ambient Sample
/// notifications.
/// \return \c true if the CCCD notification bit is set.
inline bool notifications_enabled() noexcept {
    return app_bme280_ambient_sample_client_char_config[0] &
           wiced_bt_gatt_client_char_config_e::GATT_CLIENT_CONFIG_NOTIFICATION;
}

/// \brief Write the latest reading into the Ambient Sample characteristic.
/// \param temperature_centi_degC Temperature in hundredths of a degree C.
/// \param humidity_centi_pct     Relative humidity in hundredths of a percent.
/// \param pressure_pa            Pressure in pascals.
inline void set_ambient_sample(int16_t temperature_centi_degC,
                               uint16_t humidity_centi_pct,
                               uint32_t pressure_pa) noexcept {
    bme280_sample s{temperature_centi_degC, humidity_centi_pct, pressure_pa};
    ble_gatt_db_set_value(HDLC_BME280_AMBIENT_SAMPLE_VALUE,
                          reinterpret_cast<uint8_t *>(&s),
                          static_cast<uint16_t>(sizeof(s)));
}

/// \brief Notify the connected central with the current Ambient Sample value.
/// \param connection_id BLE connection to notify.
/// \return \c wiced_bt_gatt_status_t result of the notification send.
inline wiced_bt_gatt_status_t notify(uint16_t connection_id) noexcept {
    return wiced_bt_gatt_server_send_notification(
        connection_id, HDLC_BME280_AMBIENT_SAMPLE_VALUE,
        app_bme280_ambient_sample_len, app_bme280_ambient_sample, nullptr);
}

///
/// \brief Publish a reading: update the characteristic, and notify if a central
///        is connected and has subscribed.
///
/// \param temperature_centi_degC Temperature in hundredths of a degree C.
/// \param humidity_centi_pct     Relative humidity in hundredths of a percent.
/// \param pressure_pa            Pressure in pascals.
///
inline void publish(int16_t temperature_centi_degC, uint16_t humidity_centi_pct,
                    uint32_t pressure_pa) noexcept {
    set_ambient_sample(temperature_centi_degC, humidity_centi_pct, pressure_pa);
    if (sentinel::ble_context_object.connected() && notifications_enabled()) {
        notify(sentinel::ble_context_object.connection_id());
    }
}

} // namespace sentinel::gatt::bme280

#endif /* SENTINEL_GATT_BME280_HPP */
