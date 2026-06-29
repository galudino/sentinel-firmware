///
/// \file    sentinel_battery_service_task.cpp
/// \brief   Battery Service Task implementation
///
/// \details This file implements the Battery Service FreeRTOS task that
///          periodically updates battery levels and sends BLE notifications.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - Battery service task implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cycfg_gatt_db.h"
#include "cyhal.h"

#include "wiced_bt_gatt.h"

#include <FreeRTOS.h>
}
#pragma GCC diagnostic pop

#include "sentinel_ble_context.hpp"
#include "sentinel_gatt_battery.hpp"
#include "sentinel_task_battery_service.hpp"
#include "sentinel_task_debug_stream.hpp"
#include "sentinel_utilities.hpp"

namespace {

constexpr auto BATTERY_LEVEL_CHANGE =
    uint32_t{2}; ///< Rate of change of battery level
constexpr auto BATTERY_LEVEL_UPDATE_MS =
    uint32_t{9999u}; ///< Update rate of Battery level
constexpr auto BATTERY_LEVEL_UPDATE_FREQ =
    uint32_t{10000}; ///< Update frequency

/// \brief Update battery percentage
///
/// Simulated battery level updates:
/// Battery level is read from GATT DB and is reduced by `decrease_interval`
/// percent by default, and initialized again to 100 once it reaches 0.
///
inline void
update_battery_percentage(uint8_t decrease_interval = BATTERY_LEVEL_CHANGE) {
    using namespace sentinel::gatt::battery;
    set_level(level() == 0 ? 100 : level() - decrease_interval);
}

inline void send_notification_for_battery_percentage() {
    wiced_bt_gatt_server_send_notification(
        sentinel::ble_context_object.connection_id(),
        HDLC_BAS_BATTERY_LEVEL_VALUE, app_bas_battery_level_len,
        app_bas_battery_level, nullptr);
}

} // namespace

using namespace sentinel::task;

battery_service &battery_service::instance() noexcept {
    static battery_service service;
    return service;
}

BaseType_t battery_service::task_create() noexcept {
    return xTaskCreate(&battery_service::task_trampoline, "Battery Service Task",
                       (configMINIMAL_STACK_SIZE * 4), this,
                       (configMAX_PRIORITIES - 3), &m_handle);
}

void battery_service::task_trampoline(void *task_parameter) {
    static_cast<battery_service *>(task_parameter)->run();
}

void battery_service::run() {
    // Initialize the HAL timer used to count seconds
    auto result =
        cyhal_timer_init(&m_timer, cyhal_gpio_psoc6_01_116_bga_ble_t::NC,
                         nullptr);

    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(false);
    }

    result = configure_timer();

    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(false);
    }

    // Start battery level timer
    result = cyhal_timer_start(&m_timer);

    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(false);
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!ble_context_object.connected()) {
            // Not connected, skip battery update
            continue;
        }

        if (!(app_bas_battery_level_client_char_config[0] &
              wiced_bt_gatt_client_char_config_e::
                  GATT_CLIENT_CONFIG_NOTIFICATION)) {
            // Notifications not enabled, skip battery update
            continue;
        }

        update_battery_percentage();
        send_notification_for_battery_percentage();
    }
}

void battery_service::timer_isr(void *callback_arg, cyhal_timer_event_t event) {
    sentinel::unused(event);

    auto *self = static_cast<battery_service *>(callback_arg);

    auto xHigherPriorityTaskWoken = BaseType_t{pdFALSE};
    vTaskNotifyGiveFromISR(self->m_handle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

cy_rslt_t battery_service::configure_timer() noexcept {
    const auto battery_service_timer_config = cyhal_timer_cfg_t{
        true,                                        ///< Run timer indefinitely
        cyhal_timer_direction_t::CYHAL_TIMER_DIR_UP, ///< Timer counts up
        false,                                       ///< Don't use compare mode
        BATTERY_LEVEL_UPDATE_MS, ///< Timer period in milliseconds
        0,                       ///< Timer compare value (not used)
        0                        ///< Initial counter value
    };

    // Configure the timer for battery level updates (5 seconds)
    auto result = cyhal_timer_configure(&m_timer, &battery_service_timer_config);

    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(false);
    }

    result = cyhal_timer_set_frequency(&m_timer, BATTERY_LEVEL_UPDATE_FREQ);

    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(false);
    }

    // Register for a callback whenever timer reaches terminal count. The
    // instance is passed as the callback argument so the ISR can notify this
    // task without reaching for a file-static handle.
    cyhal_timer_register_callback(&m_timer, &battery_service::timer_isr, this);

    cyhal_timer_enable_event(
        &m_timer, cyhal_timer_event_t::CYHAL_TIMER_IRQ_TERMINAL_COUNT, 3, true);

    return result;
}
