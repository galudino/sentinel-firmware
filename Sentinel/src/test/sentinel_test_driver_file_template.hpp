///
/// \file    sentinel_test_driver_file_template.hpp
/// \brief   Hardware driver test function declarations
///
/// \details This header provides test function declarations for hardware
///          driver validation. These functions are intended for use in
///          testbench builds to verify driver functionality.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - Test template
///

#ifndef SENTINEL_TEST_DRIVER_FILE_TEMPLATE_HPP
#define SENTINEL_TEST_DRIVER_FILE_TEMPLATE_HPP

namespace sentinel::test::driver {

///
/// \brief Test driver basic functionality
///
/// Main test function that exercises driver initialization and basic
/// operations. Add specific test steps as needed for the driver being tested.
///
void all();

///
/// \brief Test driver chip ID read operation
///
/// Validates that the chip ID can be successfully read from the device and
/// matches expected values.
///
void chip_id_read();

///
/// \brief Test driver read operations
///
/// Validates data read operations from the driver, including register reads
/// and sensor data acquisition.
///
void read();

///
/// \brief Test driver write operations
///
/// Validates data write operations to the driver, including register writes
/// and configuration updates.
///
void write();

} // namespace sentinel::test::driver

#endif /* SENTINEL_TEST_DRIVER_FILE_TEMPLATE_HPP */
