///
/// \file    sentinel_test_driver_file_template.cpp
/// \brief   Hardware driver test function implementations
///
/// \details This file implements test functions for hardware driver validation.
///          These functions provide a template structure for adding driver
///          tests in testbench builds.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - Test template implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_result.h"

#include "cy_scb_common.h"
#include "cy_scb_i2c.h"
#include "cy_scb_spi.h"
#include "cy_scb_uart.h"

#include "cybsp.h"
#include "cycfg_peripherals.h"
#include "cyhal.h"

#include "cyhal_gpio.h"
#include "cyhal_i2c.h"
#include "cyhal_pwm.h"
#include "cyhal_spi.h"
#include "cyhal_system.h"
#include "cyhal_uart.h"
}
#pragma GCC diagnostic pop

#include "sentinel_driver_file_template.hpp"
#include "sentinel_utilities.hpp"

#include "sentinel_test_driver_file_template.hpp"

using sentinel::driver;

void sentinel::test::driver::all() {
    // Set breakpoints while debugging to inspect values.
    //
    // Or, since we have the LCD working, display the test results on the LCD!
    //
    // Alternatively (or both) -- use SentinelPanel to display logs of running
    // tests, with our working BLE implementation.
    //
    // (create a log service, and have a characteristic send a log notification
    // to the SentinelPanel app that can then be displayed to the user.)
    chip_id_read();
    read();
    write();
}

void sentinel::test::driver::chip_id_read() {
    // TODO: Implement chip ID read test
}

void sentinel::test::driver::read() {
    // TODO: Implement driver read operations test
}

void sentinel::test::driver::write() {
    // TODO: Implement driver write operations test
}
