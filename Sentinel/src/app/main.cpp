///
/// \file    main.cpp
/// \brief   Main application entry point
///
/// \details This file contains only the main() function which initializes the
///          system hardware, OTA functionality, Bluetooth stack, and starts
///          the FreeRTOS scheduler.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - Simplified main with modular architecture
///

// Always wrap C includes in diagnostic push/pop,
// along with an extern "C" block -- to avoid pedantic warnings.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
///< Cypress/Infineon
#include "cy_log.h"
#include "cy_ota_api.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include "cybsp.h"
#include "cybt_platform_trace.h"
#include "cycfg_bt_settings.h"
#include "cycfg_pins.h"
#include "cyhal_wdt.h"

#ifdef OTA_USE_EXTERNAL_FLASH
#include "ota_serial_flash.h"
#endif

///< FreeRTOS
#include "portmacro.h"
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

///< Tasks — only the BLE debug stream is created before the scheduler (it must
///< be up before any task logs over BLE). The boot orchestrator (#38) spawns
///< and starts every other task once the scheduler is running.
#include "sentinel_task_debug_stream.hpp"

///< Boot orchestrator — one-shot boot sequence + service-task spawner (#38).
#include "sentinel_app_orchestrator.hpp"

///< Utilities
#include "sentinel_firmware_version.hpp"
#include "sentinel_utilities.hpp"

///< Drivers
#include "sentinel_led_pwm.hpp"

///< Device Configurator Resources
#include "sentinel_resource.hpp"

///< Bluetooth LE
#include "sentinel_ble_context.hpp"

namespace sentinel::app {

///
/// \brief Initialize system hardware and Bluetooth stack
///        Shouldn't have to be modified unless adding new hardware
///        initialization.
///
/// \return \c true if the BLE stack initialized successfully. POST consumes
/// this
///         (via the boot orchestrator) and records a BLE-stack failure rather
///         than bricking the boot — the device runs degraded (decision #12).
///
static inline bool initialize() {
    // Initialize the board support package (BSP).
    auto result = cybsp_init();

    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(false);
    }

    // Enable global interrupts.
    __enable_irq();

    // Initialize retarget-io to use the debug UART port.
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                        CY_RETARGET_IO_BAUDRATE);

    // default for all logging to WARNING.
    cy_log_init(CY_LOG_LEVEL_T::CY_LOG_INFO, nullptr, nullptr);

    // Set default log levels.
    cy_ota_set_log_level(CY_LOG_LEVEL_T::CY_LOG_INFO);

    // Initialize QuadSPI if using external flash.
#if defined(OTA_USE_EXTERNAL_FLASH)
    // We need to init from every ext flash write
    // See ota_serial_flash.h
    if (ota_smif_initialize() != CY_RSLT_SUCCESS) {
        CY_ASSERT(0 == 1);
    }
#endif

    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "sentinel-firmware ==============================\r\n");
    cy_log_msg(
        CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
        "Application version: %d.%d.%d.%d\n", current_firmware_version.major(),
        current_firmware_version.minor(), current_firmware_version.patch(),
        current_firmware_version.build());
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "================================================\n\n");

#ifdef TEST_REVERT
    cy_log_msg(
        CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
        "======================TESTING REVERT==========================\r\n");
    cy_log_msg(
        CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
        "===============================================================\r\n");
    cy_log_msg(
        CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
        "===============================================================\r\n");
    cy_log_msg(
        CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
        "=========================== Rebooting !!!======================\r\n");
    cy_log_msg(
        CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
        "===============================================================\r\n");
    NVIC_SystemReset();
#else
    // Validate the update so we do not revert on reboot.
    cy_ota_storage_validated();
#endif

    auto wdt_obj = cyhal_wdt_t{};
    cyhal_wdt_init(&wdt_obj, cyhal_wdt_get_max_timeout_ms());

    // Clear watchdog so it doesn't reboot on us.
    cyhal_wdt_free(&wdt_obj);

    // Initialize resources.
    resource::peripheral_initialize();

    // Start the BLE debug output stream task. Must be running before any
    // task tries to send log messages over BLE notifications.
    auto debug_stream_result = task::debug_stream::instance().task_create();
    configASSERT(debug_stream_result == pdPASS);

    // Initialize Bluetooth LE stack and services
    // Register callback and configuration with stack.
    auto wiced_result = ble_context_object.stack_initialize();

    const auto ble_stack_ok = wiced_result == wiced_result_t::WICED_BT_SUCCESS;
    if (!ble_stack_ok) {
        // Do NOT brick the boot: POST records the BLE-stack failure and the
        // device runs degraded (decision #12). All non-BLE subsystems still
        // come up through the boot orchestrator.
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "*** Bluetooth stack initialization failed! ***\r\n");
    }

    return ble_stack_ok;
}

} // namespace sentinel::app

///
/// \brief Application entry point
///
/// Initializes the device hardware, OTA functionality, Bluetooth stack,
/// creates all tasks, and starts the FreeRTOS scheduler.
///
/// \return Application exit status (never returns in normal operation)
///
int main(int argc, const char *argv[]) {
    sentinel::unused(argc);
    sentinel::unused(argv);

    const auto ble_stack_ok = sentinel::app::initialize();

    // Create the one-shot boot orchestrator. Created here before the scheduler
    // starts, but its body runs only once scheduling begins — so the bus
    // arbiters can pump the I/O its POST probes + device-context construction
    // issue (decision #13). Phase I has no custom GATT DB yet (#6), so the
    // GATT-DB-OK argument tracks the stack-init result.
    auto orchestrator_result =
        sentinel::app::boot_orchestrator::instance().task_create(ble_stack_ok,
                                                                 ble_stack_ok);
    configASSERT(orchestrator_result == pdPASS);

    // Start the FreeRTOS scheduler.
    vTaskStartScheduler();

    // Should never arrive here.
    CY_ASSERT(false);
}
