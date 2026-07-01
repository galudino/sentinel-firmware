///
/// \file    sentinel_resource.cpp
/// \brief   Shared system bring-up (resource::system_initialize)
///
/// \details Defines \ref sentinel::resource::system_initialize, hoisted out of
///          the target entry point (now the single \c src/main.cpp, #51) so the
///          boot bring-up sequence has a single source of truth shared by both
///          the main-firmware and testbench builds. Kept in a \c .cpp
///          (not the header) because it depends on the BLE stack, OTA, and
///          retarget-IO headers, which must not leak into every translation unit
///          that includes \c sentinel_resource.hpp (transitively, that is nearly
///          everything, via \c sentinel_device_context.hpp).
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - Hoisted shared system bring-up
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
///< Cypress/Infineon
#include "cy_log.h"
#include "cy_ota_api.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include "cybsp.h"
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

#include "sentinel_ble_context.hpp"
#include "sentinel_firmware_version.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_task_debug_stream.hpp"

// APP_NAME_STRING arrives as a bare token (the Makefile's quotes are stripped by
// the shell), e.g. `sentinel-firmware` — not a string literal. Stringize it so
// it can be interpolated with %s. The two-level indirection expands the macro
// first, then converts the resulting token sequence to a string literal.
#define SENTINEL_STRINGIZE2(x) #x
#define SENTINEL_STRINGIZE(x) SENTINEL_STRINGIZE2(x)

namespace sentinel::resource {

bool system_initialize() noexcept {
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

    // Default all logging to INFO.
    cy_log_init(CY_LOG_LEVEL_T::CY_LOG_INFO, nullptr, nullptr);
    cy_ota_set_log_level(CY_LOG_LEVEL_T::CY_LOG_INFO);

    // Initialize QuadSPI if using external flash (needed before every ext-flash
    // write — see ota_serial_flash.h).
#if defined(OTA_USE_EXTERNAL_FLASH)
    if (ota_smif_initialize() != CY_RSLT_SUCCESS) {
        CY_ASSERT(0 == 1);
    }
#endif

    // Banner — APP_NAME_STRING is the build define ("sentinel-firmware" or
    // "sentinel-testbench"), so one definition serves both targets.
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "%s ==============================\r\n",
               SENTINEL_STRINGIZE(APP_NAME_STRING));
    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "Application version: %d.%d.%d.%d\n",
               current_firmware_version.major(),
               current_firmware_version.minor(),
               current_firmware_version.patch(),
               current_firmware_version.build());
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "================================================\n\n");

#ifdef TEST_REVERT
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "======================TESTING REVERT==========================\r\n");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "=========================== Rebooting !!!======================\r\n");
    NVIC_SystemReset();
#else
    // Validate the update so we do not revert on reboot.
    cy_ota_storage_validated();
#endif

    // Kick the watchdog so it does not reboot us during bring-up.
    auto wdt_obj = cyhal_wdt_t{};
    cyhal_wdt_init(&wdt_obj, cyhal_wdt_get_max_timeout_ms());
    cyhal_wdt_free(&wdt_obj);

    // Bus arbiters + flash device mutex.
    peripheral_initialize();

    // Start the BLE debug output stream task. Must be running before any task
    // tries to send log messages over BLE notifications.
    auto debug_stream_result = task::debug_stream::instance().task_create();
    configASSERT(debug_stream_result == pdPASS);

    // Initialize the Bluetooth LE stack (register callback + configuration).
    auto wiced_result = ble_context_object.stack_initialize();

    const auto ble_stack_ok = wiced_result == wiced_result_t::WICED_BT_SUCCESS;
    if (!ble_stack_ok) {
        // Do NOT brick the boot: POST records the BLE-stack failure and the
        // device runs degraded (decision #12). All non-BLE subsystems still come
        // up through the orchestrator.
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "*** Bluetooth stack initialization failed! ***\r\n");
    }

    return ble_stack_ok;
}

} // namespace sentinel::resource
