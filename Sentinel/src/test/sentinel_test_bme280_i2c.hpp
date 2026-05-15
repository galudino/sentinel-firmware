#ifndef SENTINEL_TEST_BME280_I2C_HPP
#define SENTINEL_TEST_BME280_I2C_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "bme280.h"
#include "cy_log.h"
#include "cy_result.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "bme280_i2c.hpp"
#include "sentinel_resource.hpp"

namespace sentinel::test::bme280_i2c {

static inline BaseType_t task_create() {
    return xTaskCreate(
        [](void *) -> void {
            bme280_dev dev;
            sentinel::temp::bosch_i2c::cyhal::bme280_init_cyhal(&dev);

            auto result = bme280_init(&dev);

            if (result == CY_RSLT_SUCCESS) {
                cy_log_msg(
                    CYLF_DEF, CY_LOG_INFO,
                    "[BME280] I2C read successful: chip_id=0x%02X (%d)\n",
                    dev.chip_id, dev.chip_id);
            } else {
                cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                           "[BME280] I2C read failed with error: %d\n", result);
            }
        },
        "BME280 Test Task", configMINIMAL_STACK_SIZE, nullptr,
        (configMAX_PRIORITIES - 3), nullptr);
}

void init();

} // namespace sentinel::test::bme280_i2c

#endif /* SENTINEL_TEST_BME280_I2C_HPP */
