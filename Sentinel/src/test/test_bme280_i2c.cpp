#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_result.h"
}

#include "resource.hpp"
#include "test_bme280_i2c.hpp"

using namespace sentinel;

void test::bme280_i2c::init() {
    struct bme280_dev dev;

    dev.intf = BME280_I2C_INTF;
    dev.intf_ptr = &resource::cybsp_i2c;

    dev.read = sentinel::temp::bosch_i2c::cyhal::write_read;
    dev.write = sentinel::temp::bosch_i2c::cyhal::write;
    dev.delay_us = sentinel::temp::bosch_i2c::cyhal::delay_us;

    auto result = bme280_init(&dev);

    if (result == CY_RSLT_SUCCESS) {
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "I2C read successful, ID: 0x%02X (%d)\n", dev.chip_id,
                   dev.chip_id);
    } else {
        cy_log_msg(CYLF_DEF, CY_LOG_ERR, "I2C read failed with error: %d\n",
                   result);
    }
}
