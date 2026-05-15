///
/// \file    sentinel_driver_file_template.hpp
/// \brief   Hardware driver template interface
///
/// \details This header provides a template structure for creating new hardware
///          device drivers. It includes standard includes and class structure
///          for peripheral drivers.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - Driver template
///

#ifndef SENTINEL_DRIVER_FILE_TEMPLATE_HPP
#define SENTINEL_DRIVER_FILE_TEMPLATE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_result.h"
#include "cycfg_peripherals.h"
#include "cycfg_pins.h"
#include "cyhal_hw_types.h"
#include "cyhal_psoc6_01_116_bga_ble.h"
}
#pragma GCC diagnostic pop

#include "sentinel_utilities.hpp"

namespace sentinel {

class driver;

} // namespace sentinel

///
/// \brief Hardware driver template class
///
/// This class serves as a template for implementing hardware device drivers.
/// It provides a standard structure with public interface methods, private
/// implementation details, and protected helper functions.
///
/// To use this template:
/// 1. Replace "driver" with your specific driver name. Remember to replace
/// DRIVER_FILE_TEMPLATE too.
/// 2. Add public interface methods for device operations
/// 3. Add private member variables for device state and handles
/// 4. Add protected helper methods for internal operations
///
class sentinel::driver {
public:
    // TODO: Add public interface methods (initialization, configuration,
    // operations)

private:
    // TODO: Add private member variables (device handles, state, configuration)

protected:
    // TODO: Add protected helper methods (internal operations, utilities)
};

#endif /* SENTINEL_DRIVER_FILE_TEMPLATE_HPP */
