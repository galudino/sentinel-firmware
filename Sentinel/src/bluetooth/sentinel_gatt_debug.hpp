///
/// \file    sentinel_gatt_debug.hpp
/// \brief   Bluetooth LE public interface for characteristic values in the
/// debug service
///
/// \details This header provides the public interface for accessing and
/// updating the characteristic values in the debug service.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0
///

#ifndef SENTINEL_GATT_DEBUG_HPP
#define SENTINEL_GATT_DEBUG_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cycfg_gatt_db.h"

#include "wiced_bt_gatt.h"
}
#pragma GCC diagnostic pop

#include "sentinel_span.hpp"

namespace sentinel::gatt::debug {

/// \brief Access the raw debug output stream characteristic buffer.
/// \return Span over the GATT DB's debug output stream storage.
inline sentinel::span<char> output_stream() noexcept {
    return {reinterpret_cast<char *>(app_debug_output_stream),
            app_debug_output_stream_len};
}

/// \brief Read the application-side "notify enabled" flag for the debug
///        output stream.
/// \return \c true if the flag is set.
inline bool output_notify_stream_enabled() noexcept {
    return app_debug_output_stream_notify_enable[0] != 0;
}

/// \brief Set the application-side "notify enabled" flag for the debug
///        output stream.
/// \param enabled \c true to enable, \c false to disable.
inline void set_output_notify_stream_enabled(bool enabled) noexcept {
    app_debug_output_stream_notify_enable[0] = enabled ? 1 : 0;
}

/// \brief Check whether the GATT client has enabled notifications (CCCD)
///        for the debug output stream characteristic.
/// \return \c true if the CCCD notification bit is set.
inline bool output_stream_notifications_enabled() noexcept {
    return app_debug_output_stream_client_char_config[0] &
           wiced_bt_gatt_client_char_config_e::GATT_CLIENT_CONFIG_NOTIFICATION;
}

} // namespace sentinel::gatt::debug

#endif /* SENTINEL_GATT_DEBUG_HPP */
