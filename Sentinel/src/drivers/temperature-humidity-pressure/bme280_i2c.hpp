#ifndef BME280_I2C_HPP
#define BME280_I2C_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "bme280.h"
#include "cy_result.h"
#include "cyhal_i2c.h"
#include "cyhal_system_impl.h"
}
#pragma GCC diagnostic pop

#include "sentinel_resource.hpp"
#include "sentinel_utilities.hpp"

#include <algorithm>
#include <vector>

namespace sentinel::temp::bosch_i2c::cyhal {

inline int8_t write_read(uint8_t reg_addr, uint8_t *rx, uint32_t rx_size,
                         void *intf_ptr) {
    auto *i2c = static_cast<cyhal_i2c_t *>(intf_ptr);

    auto result = cyhal_i2c_master_write(i2c, BME280_I2C_ADDR_PRIM, &reg_addr,
                                         sizeof(reg_addr), 0, false);

    if (result != CY_RSLT_SUCCESS) {
        return result;
    }

    return cyhal_i2c_master_read(i2c, BME280_I2C_ADDR_PRIM, rx, rx_size, 0,
                                 true);
}

inline int8_t write(uint8_t reg_addr, const uint8_t *tx, uint32_t tx_size,
                    void *intf_ptr) {
    auto i2c = static_cast<cyhal_i2c_t *>(intf_ptr);

    // Allocate buffer to combine register address and data
    // I2C write operations require register address followed by
    // write_data bytes
    auto buffer = std::vector<uint8_t>(tx_size + 1);

    // Prepare the complete write buffer: [register_address,
    // write_data]
    buffer[0] = reg_addr;
    std::copy(tx, tx + tx_size, buffer.data() + 1);

    // Perform the complete I2C write operation in a single
    // transaction.
    // This ensures atomic register write operation.
    auto result = cyhal_i2c_master_write(
        i2c, BME280_I2C_ADDR_PRIM, buffer.data(), tx_size + 1, 1000, false);

    return result;
}

inline void delay_us(uint32_t period, void *intf_ptr) {
    sentinel::unused(intf_ptr);
    cyhal_system_delay_us(period);
}

inline void bme280_init_cyhal(bme280_dev *const dev) {
    dev->intf = BME280_I2C_INTF;
    dev->intf_ptr = &sentinel::resource::cybsp_i2c;

    dev->read = write_read;
    dev->write = write;
    dev->delay_us = delay_us;
}

} // namespace sentinel::temp::bosch_i2c::cyhal

#endif /* BME280_DRIVER_HPP */
