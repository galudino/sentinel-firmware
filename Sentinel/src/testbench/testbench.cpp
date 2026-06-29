///
/// \file    testbench.cpp
/// \brief   Testbench application entry point
///
/// \details This file contains only the main() function which initializes the
///          system hardware, OTA functionality, Bluetooth stack, and starts
///          the FreeRTOS scheduler. Test tasks are created here to validate IC
///          functionality.
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

///< Tasks
#include "sentinel_task_battery_service.hpp"
#include "sentinel_task_bme280_service.hpp"
#include "sentinel_task_debug_stream.hpp"
#include "sentinel_task_rtc_service.hpp"

///< Tests
#include "sentinel_test_bme280.hpp"
#include "sentinel_test_device_snapshot.hpp"
#include "sentinel_test_ds3231.hpp"
#include "sentinel_test_post.hpp"
#include "sentinel_test_record_store.hpp"
#include "sentinel_test_system_event_log.hpp"
#include "sentinel_test_w25q128.hpp"

///< Utilities
#include "sentinel_firmware_version.hpp"
#include "sentinel_utilities.hpp"

///< Drivers
#include "sentinel_led_pwm.hpp"

///< Device Configurator Resources
#include "sentinel_resource.hpp"

///< Bluetooth LE
#include "sentinel_ble_context.hpp"

namespace sentinel::testbench {

///
/// \brief Create test tasks
///
static inline void create_tests() {
    BaseType_t rtos_result{};

    // NOTE: To exercise a single chip's tests in isolation, comment out
    // the task_create() calls for the others below. A unified test-
    // selection menu over the retarget-io UART is planned but not yet
    // wired up.

#ifdef CYBSP_I2C_HW
    rtos_result = test::bme280::task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "BME280 test task creation failed\n");
    }

    rtos_result = test::ds3231::task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "DS3231 test task creation failed\n");
    }
#endif /* CYBSP_I2C_HW */

#ifdef CYBSP_SPI_HW
    rtos_result = test::w25q128::task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "W25Q128 test task creation failed\n");
    }

    rtos_result = test::record_store::task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "Record store test task creation failed\n");
    }
#endif /* CYBSP_SPI_HW */

    // The System Event Log suite runs against a RAM-backed record store, so it
    // is independent of the SPI flash being present.
    rtos_result = test::system_event_log::task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "System event log test task creation failed\n");
    }

    // The POST suite drives the probes with fake driver doubles, so it too is
    // independent of any physical sensor / flash being present.
    rtos_result = test::post::task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "POST test task creation failed\n");
    }

    // The device_snapshot suite (#36) exercises the packed wire layout in RAM,
    // so it needs no physical sensor either.
    rtos_result = test::device_snapshot::task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "device_snapshot test task creation failed\n");
    }
}

///
/// \brief Create application tasks
///
static inline void create_tasks() {
    BaseType_t rtos_result{};

    rtos_result = sentinel::task::battery_service::instance().task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "Battery service task creation failed\n");
    }

    rtos_result = sentinel::task::rtc_service::instance().task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "RTC service task creation failed\n");
    }

    // BME280 sample service (#37): caches the latest reading for the device
    // snapshot populate() and the live BLE characteristic. The driver smoke
    // test in create_tests() still owns its own instance; the bus arbiter
    // serializes the two. Comment one out to isolate on-bench.
    rtos_result = sentinel::task::bme280_service::instance().task_create();

    if (rtos_result != pdPASS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "BME280 service task creation failed\n");
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

#ifdef TEST_REVERT
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "======================TESTING "
               "REVERT==========================\r\n");
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "==========================================================="
               "====\r\n");
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "==========================================================="
               "====\r\n");
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "=========================== Rebooting "
               "!!!======================\r\n");
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "==========================================================="
               "====\r\n");
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

    if (wiced_result != wiced_result_t::WICED_BT_SUCCESS) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "Bluetooth Stack Initialization failed!! \r\n");
        CY_ASSERT(false);
    }

    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "sentinel-testbench =============================\r\n");
    cy_log_msg(
        CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
        "Application version: %d.%d.%d.%d\n", current_firmware_version.major(),
        current_firmware_version.minor(), current_firmware_version.patch(),
        current_firmware_version.build());
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "================================================\n\n");
}
} // namespace sentinel::testbench

///
/// \brief Application entry point
///
/// Initializes the device hardware, OTA functionality, Bluetooth stack,
/// creates all tasks, creates all tests, and starts the FreeRTOS scheduler.
///
/// \return Application exit status (never returns in normal operation)
///
int main(int argc, const char *argv[]) {
    sentinel::unused(argc);
    sentinel::unused(argv);

    sentinel::testbench::initialize();
    sentinel::testbench::create_tasks();
    sentinel::testbench::create_tests();

    // Start the FreeRTOS scheduler.
    vTaskStartScheduler();

    // Should never arrive here.
    CY_ASSERT(false);
}
