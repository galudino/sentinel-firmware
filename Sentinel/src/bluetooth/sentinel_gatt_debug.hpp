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

static inline sentinel::span<char> output_stream() {
    return {reinterpret_cast<char *>(app_debug_output_stream),
            app_debug_output_stream_len};
}

static inline bool output_notify_stream_enabled() {
    return app_debug_output_stream_notify_enable[0] != 0;
}

static inline void set_output_notify_stream_enabled(bool enabled) {
    app_debug_output_stream_notify_enable[0] = enabled ? 1 : 0;
}

static inline bool output_stream_notifications_enabled() {
    return app_debug_output_stream_client_char_config[0] &
           wiced_bt_gatt_client_char_config_e::GATT_CLIENT_CONFIG_NOTIFICATION;
}

} // namespace sentinel::gatt::debug

#endif /* SENTINEL_GATT_DEBUG_HPP */
