///
/// \file    main.cpp
/// \brief   Main application entry point
///
/// \details This file contains only the main() function which initializes the
///          system hardware, OTA functionality, Bluetooth stack, and starts
///          the FreeRTOS scheduler.
///
/// \author  galudino
/// \date    2025
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

///< Tasks
#include "battery_service_task.hpp"

///< Utilities
#include "utilities.hpp"

///< Drivers
#include "led_pwm.hpp"

///< Device Configurator Resources
#include "resource.hpp"

///< Bluetooth LE
#include "ble_context.hpp"

///
/// \brief Create application tasks
///
static inline void create_tasks() {
    BaseType_t rtos_result{};

    rtos_result = battery_service_task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CYLF_DEF, CY_LOG_ERR, "BAS task creation failed\n");
    }
}

///
/// \brief Initialize system hardware and Bluetooth stack
///        Shouldn't have to be modified unless adding new hardware
///        initialization.
///
static inline void initialize() {
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
    cy_log_init(CY_LOG_INFO, NULL, NULL);

    // Set default log levels.
    cy_ota_set_log_level(CY_LOG_INFO);

    // Initialize QuadSPI if using external flash.
#if defined(OTA_USE_EXTERNAL_FLASH)
    // We need to init from every ext flash write
    // See ota_serial_flash.h
    if (ota_smif_initialize() != CY_RSLT_SUCCESS) {
        CY_ASSERT(0 == 1);
    }
#endif

#ifdef TEST_REVERT
    cy_log_msg(
        CYLF_DEF, CY_LOG_INFO,
        "======================TESTING REVERT==========================\r\n");
    cy_log_msg(
        CYLF_DEF, CY_LOG_INFO,
        "===============================================================\r\n");
    cy_log_msg(
        CYLF_DEF, CY_LOG_INFO,
        "===============================================================\r\n");
    cy_log_msg(
        CYLF_DEF, CY_LOG_INFO,
        "=========================== Rebooting !!!======================\r\n");
    cy_log_msg(
        CYLF_DEF, CY_LOG_INFO,
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

    // Initialize Bluetooth LE stack and services
    // Register callback and configuration with stack.
    auto wiced_result = ble_context_object.stack_initialize();

    if (wiced_result != wiced_result_t::WICED_BT_SUCCESS) {
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "Bluetooth Stack Initialization failed!! \r\n");
        CY_ASSERT(false);
    }

    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "========= BTStack FreeRTOS Example =============\r\n");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "======= Battery Server Application Start =======\r\n");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "================================================\n");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "Application version: %d.%d.%d.%d\n",
               APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_BUILD,
               APP_VERSION_PATCH);
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "================================================\n\n");
}

///
/// \brief Application entry point
///
/// Initializes the device hardware, OTA functionality, Bluetooth stack,
/// creates the battery service task, and starts the FreeRTOS scheduler.
///
/// \return Application exit status (never returns in normal operation)
///
int main(int argc, const char *argv[]) {
    util::unused(argc);
    util::unused(argv);

    initialize();
    create_tasks();

    // Start the FreeRTOS scheduler.
    vTaskStartScheduler();

    // Should never arrive here.
    CY_ASSERT(false);
}
