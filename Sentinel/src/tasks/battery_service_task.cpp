///
/// \file    battery_service_task.cpp
/// \brief   Battery Service Task implementation
///
/// \details This file implements the Battery Service FreeRTOS task that
///          periodically updates battery levels and sends BLE notifications.
///
/// \author  galudino
/// \date    2025
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

#include "battery_service_task.hpp"
#include "ble_context.hpp"
#include "utilities.hpp"

constexpr auto BATTERY_LEVEL_CHANGE =
    uint32_t(2); ///< Rate of change of battery level
constexpr auto BATTERY_LEVEL_UPDATE_MS =
    uint32_t(9999u); ///< Update rate of Battery level
constexpr auto BATTERY_LEVEL_UPDATE_FREQ =
    uint32_t(10000); ///< Update frequency

static auto battery_service_timer = cyhal_timer_t{}; ///< 5 sec timer object

///
/// \brief Timer callback function
///
/// This callback function is invoked on timeout of 1 second timer.
///
/// \param callback_argument Unused
/// \param timer_event Unused
///
/// \return void
///
static void battery_service_timer_callback(void *callback_argument,
                                           cyhal_timer_event_t timer_event);

///
/// \brief Update battery percentage
///
/// Simulated battery level updates:
/// Battery level is read from GATT DB and is reduced by `decrease_interval`
/// percent by default, and initialized again to 100 once it reaches 0.
///
static void battery_service_update_percentage(
    uint8_t decrease_interval = BATTERY_LEVEL_CHANGE);

BaseType_t battery_service_task_create(void) {
    auto result =
        xTaskCreate(battery_service_task, "Battery Service Task",
                    (configMINIMAL_STACK_SIZE * 4), nullptr,
                    (configMAX_PRIORITIES - 3), &battery_service_task_handle);
    return result;
}

void battery_service_task(void *task_parameter) {
    util::unused(task_parameter);

    // Initialize the HAL timer used to count seconds
    auto result = cyhal_timer_init(&battery_service_timer, NC, nullptr);

    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(false);
    }

    ///
    /// \brief Configure timer for 5 sec
    ///
    const auto battery_service_timer_config = cyhal_timer_cfg_t{
        true,                                        ///< Run timer indefinitely
        cyhal_timer_direction_t::CYHAL_TIMER_DIR_UP, ///< Timer counts up
        false,                                       ///< Don't use compare mode
        BATTERY_LEVEL_UPDATE_MS, ///< Timer period in milliseconds
        0,                       ///< Timer compare value (not used)
        0                        ///< Initial counter value
    };

    // Configure the timer for battery level updates (5 seconds)
    cyhal_timer_configure(&battery_service_timer,
                          &battery_service_timer_config);

    result = cyhal_timer_set_frequency(&battery_service_timer,
                                       BATTERY_LEVEL_UPDATE_FREQ);

    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(false);
    }

    // Register for a callback whenever timer reaches terminal count
    cyhal_timer_register_callback(&battery_service_timer,
                                  battery_service_timer_callback, nullptr);

    cyhal_timer_enable_event(
        &battery_service_timer,
        cyhal_timer_event_t::CYHAL_TIMER_IRQ_TERMINAL_COUNT, 3, true);

    // Start battery level timer
    if (cyhal_timer_start(&battery_service_timer) != CY_RSLT_SUCCESS) {
        CY_ASSERT(false);
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!ble_context_object.connection_id()) {
            // Not connected, skip battery update
            continue;
        }

        if (!(app_bas_battery_level_client_char_config[0] &
              wiced_bt_gatt_client_char_config_e::
                  GATT_CLIENT_CONFIG_NOTIFICATION)) {
            // Notifications not enabled, skip battery update
            continue;
        }

        battery_service_update_percentage();

        wiced_bt_gatt_server_send_notification(
            ble_context_object.connection_id(), HDLC_BAS_BATTERY_LEVEL_VALUE,
            app_bas_battery_level_len, app_bas_battery_level, nullptr);
    }
}

static void battery_service_timer_callback(void *callback_argument,
                                           cyhal_timer_event_t timer_event) {
    util::unused(callback_argument);
    util::unused(timer_event);

    auto xHigherPriorityTaskWoken = BaseType_t{};
    xHigherPriorityTaskWoken = pdFALSE;

    vTaskNotifyGiveFromISR(battery_service_task_handle,
                           &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void battery_service_update_percentage(uint8_t decrease_interval) {
    app_bas_battery_level[0] =
        app_bas_battery_level[0] == 0
            ? 100
            : app_bas_battery_level[0] - decrease_interval;
}
