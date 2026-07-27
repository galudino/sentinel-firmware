///
/// \file    sentinel_cyhal_i2c_transport.hpp
/// \brief   CYHAL (Cypress Hardware Abstraction Layer) I2C transport
/// implementation
///
/// \details This header provides an I2C transport implementation using the
/// Cypress
///          Hardware Abstraction Layer (CYHAL). It implements the platform-
///          agnostic byte transport interface for PSoC 6 I2C master mode,
///          supporting configurable clock frequencies and target device
///          addressing.
///
/// \author  galudino
/// \date    2021-2024
/// \version 1.0 - CYHAL I2C transport implementation
///

#ifndef SENTINEL_CYHAL_I2C_TRANSPORT_HPP
#define SENTINEL_CYHAL_I2C_TRANSPORT_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cyhal_i2c.h"
#include "cyhal_system.h"
}
#pragma GCC diagnostic pop

#include "sentinel_byte_transport.hpp"
#include "sentinel_utilities.hpp"

#include <cstdint>

namespace sentinel {

class cyhal_i2c_transport;

} // namespace sentinel

///
/// \brief CYHAL-based I2C master transport implementation
///
/// \details Implements I2C master mode communication using the Cypress Hardware
///          Abstraction Layer. Supports standard I2C operations including
///          write, read, and repeated-start sequences for accessing target
///          devices.
///
class sentinel::cyhal_i2c_transport
    : public byte_transport<cyhal_i2c_transport, i2c_tag> {
public:
    ///
    /// \brief Construct I2C transport with CYHAL object and target address
    ///
    /// \param i2c_object Pointer to initialized CYHAL I2C object
    /// \param target_address I2C target device address (7-bit or 10-bit)
    ///
    explicit cyhal_i2c_transport(cyhal_i2c_t *i2c_object,
                                 uint16_t target_address) noexcept
        : m_i2c_object(i2c_object), m_target_address(target_address) {}

    ///
    /// \brief Get current target device address
    ///
    /// \return Target device address
    ///
    uint8_t target_address() const noexcept { return m_target_address; }

    ///
    /// \brief Configure I2C clock frequency
    ///
    /// \param frequency_hz Desired I2C clock frequency in Hertz
    ///                     (typically 100kHz or 400kHz)
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t configure(uint32_t frequency_hz) noexcept {
        auto config = cyhal_i2c_cfg_t{false, 0, frequency_hz};
        return cyhal_i2c_configure(m_i2c_object, &config);
    }

    ///
    /// \brief Set target device address
    ///
    /// \param target_address New I2C target device address
    /// \return cy_rslt_t CY_RSLT_SUCCESS (always succeeds)
    ///
    cy_rslt_t set_target_address(uint16_t target_address) noexcept {
        m_target_address = target_address;
        return CY_RSLT_SUCCESS;
    }

    ///
    /// \brief Write bytes to I2C target device
    ///
    /// \param tx Pointer to transmit buffer
    /// \param size Number of bytes to transmit
    /// \param timeout Timeout in milliseconds (0 for no timeout)
    /// \param send_stop true to send stop condition, false for repeated start
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t write(const uint8_t *tx, size_t size, uint32_t timeout = 0,
                    bool send_stop = true) noexcept {
        return cyhal_i2c_master_write(m_i2c_object, m_target_address, tx, size,
                                      timeout, send_stop);
    }

    ///
    /// \brief Bosch Sensortec API write wrapper.
    ///
    /// \details Combines \p reg_addr and \p reg_data into a single
    ///          contiguous transmit buffer (256-byte scratch buffer) and
    ///          issues one atomic I2C write transaction.
    ///
    /// \param reg_addr Starting register address to write.
    /// \param reg_data Pointer to data buffer to write.
    /// \param length   Number of bytes to write.
    /// \param intf_ptr Interface pointer; must point to a
    ///                 \c cyhal_i2c_transport instance.
    /// \return Bosch API compatible result code (0 = success).
    ///
    static int8_t bosch_write(uint8_t reg_addr, const uint8_t *reg_data,
                              uint32_t length, void *intf_ptr) noexcept {
        auto *self = static_cast<cyhal_i2c_transport *>(intf_ptr);
        // Allocate buffer to combine register address and data
        // I2C write operations require register address followed by write_data
        // bytes
        auto buffer = std::array<uint8_t, 256>();

        // Prepare the complete write buffer: [register_address, write_data]
        buffer[0] = reg_addr;
        std::copy(reg_data, reg_data + length, buffer.data() + 1);

        // Perform the complete I2C write operation in a single transaction
        // This ensures atomic register write operation
        auto result = self->write(buffer.data(), length + 1);

        return result;
    }

    ///
    /// \brief Read bytes from I2C target device
    ///
    /// \param rx Pointer to receive buffer
    /// \param size Number of bytes to read
    /// \param timeout Timeout in milliseconds (0 for no timeout)
    /// \param send_stop true to send stop condition after read
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t read(uint8_t *rx, size_t size, uint32_t timeout = 0,
                   bool send_stop = true) noexcept {
        return cyhal_i2c_master_read(m_i2c_object, m_target_address, rx, size,
                                     timeout, send_stop);
    }

    ///
    /// \brief Bosch Sensortec API read wrapper.
    ///
    /// \details Issues a write of \p reg_addr followed by a repeated-start
    ///          read of \p length bytes into \p reg_data.
    ///
    /// \param reg_addr Starting register address to read from.
    /// \param reg_data Pointer to buffer for read data.
    /// \param length   Number of bytes to read.
    /// \param intf_ptr Interface pointer; must point to a
    ///                 \c cyhal_i2c_transport instance.
    /// \return Bosch API compatible result code (0 = success).
    ///
    static int8_t bosch_read(uint8_t reg_addr, uint8_t *reg_data,
                             uint32_t length, void *intf_ptr) noexcept {
        auto *self = static_cast<cyhal_i2c_transport *>(intf_ptr);
        auto result = self->write_read(&reg_addr, sizeof(reg_addr), reg_data,
                                       length, 100, false, true);
        return result;
    }

    ///
    /// \brief Write then read with repeated start
    ///
    /// \details Performs an I2C write operation followed by a read without
    ///          releasing the bus (repeated start condition). Common for
    ///          register-based devices where you write a register address
    ///          then read its contents.
    ///
    /// \param tx Pointer to transmit buffer (typically register address)
    /// \param tx_size Number of bytes to transmit
    /// \param rx Pointer to receive buffer
    /// \param rx_size Number of bytes to receive
    /// \param timeout_on_write Timeout for write phase in milliseconds
    /// \param timeout_on_read Timeout for read phase in milliseconds
    /// \param send_stop_on_write Usually false to maintain bus control
    /// \param send_stop_on_read Usually true to release bus after read
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t write_read(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                         size_t rx_size, uint32_t timeout_on_write = 0,
                         uint32_t timeout_on_read = 0,
                         bool send_stop_on_write = false,
                         bool send_stop_on_read = true) noexcept {
        auto result =
            cyhal_i2c_master_write(m_i2c_object, m_target_address, tx, tx_size,
                                   timeout_on_write, send_stop_on_write);
        if (result != CY_RSLT_SUCCESS) {
            return result;
        }

        return cyhal_i2c_master_read(m_i2c_object, m_target_address, rx,
                                     rx_size, timeout_on_read,
                                     send_stop_on_read);
    }

    ///
    /// \brief Transfer data (non-blocking)
    ///
    /// \param tx Span of bytes to transmit
    /// \param rx Span for receive buffer
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t transfer_async(span<const uint8_t> tx,
                             span<uint8_t> rx) noexcept {
        return cyhal_i2c_master_transfer_async(m_i2c_object, m_target_address,
                                               tx.data(), tx.size(), rx.data(),
                                               rx.size());
    }

    ///
    /// \brief Delay
    ///
    /// \param milliseconds Delay in milliseconds
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t delay(uint32_t milliseconds) noexcept {
        return cyhal_system_delay_ms(milliseconds);
    }

    ///
    /// \brief Delay
    ///
    /// \param microseconds Delay in microseconds
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t delay_us(uint32_t microseconds) noexcept {
        cyhal_system_delay_us(microseconds);
        return CY_RSLT_SUCCESS;
    }

    ///
    /// \brief Bosch Sensortec API delay wrapper.
    ///
    /// \details Bosch callbacks express their period in microseconds;
    ///          CYHAL's system delay is millisecond-granular, so the
    ///          period is rounded up (minimum 1 ms).
    ///
    /// \param period   Delay duration in microseconds.
    /// \param intf_ptr Interface pointer; must point to a
    ///                 \c cyhal_i2c_transport instance.
    ///
    static void bosch_delay(uint32_t period, void *intf_ptr) noexcept {
        // Interface pointer is unused but required by Bosch API signature
        auto *self = static_cast<cyhal_i2c_transport *>(intf_ptr);

        // Convert microseconds to milliseconds with proper rounding
        // PSoC HAL only supports millisecond delays, so we round up
        auto delay_ms = (period + 999) / 1000;

        // Ensure minimum delay of 1ms for very short microsecond delays
        if (delay_ms == 0) {
            delay_ms = 1;
        }

        // Perform the delay using PSoC HAL system delay function
        self->delay(delay_ms);
    }

private:
    cyhal_i2c_t *m_i2c_object; ///< Pointer to CYHAL I2C object
    uint16_t m_target_address; ///< Current I2C target device address
};

#endif /* SENTINEL_CYHAL_I2C_TRANSPORT_HPP */
