///
/// \file    sentinel_cyhal_spi_transport.hpp
/// \brief   CYHAL (Cypress Hardware Abstraction Layer) SPI transport
/// implementation
///
/// \details This header provides an SPI transport implementation using the
/// Cypress
///          Hardware Abstraction Layer (CYHAL). It implements the platform-
///          agnostic byte transport interface for PSoC 6 SPI master mode,
///          supporting configurable clock frequencies, SPI modes, and optional
///          chip select control.
///
/// \author  galudino
/// \date    2021-2026
/// \version 1.1 - Added Bosch Sensortec API callback wrappers (bosch_read/write/delay)
///

#ifndef SENTINEL_CYHAL_SPI_TRANSPORT_HPP
#define SENTINEL_CYHAL_SPI_TRANSPORT_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_result.h"
#include "cy_scb_spi.h"
#include "cycfg_peripherals.h"
#include "cyhal_gpio.h"
#include "cyhal_spi.h"
#include "cyhal_system.h"
}
#pragma GCC diagnostic pop

#include "sentinel_byte_transport.hpp"
#include "sentinel_utilities.hpp"

#include <array>
#include <algorithm>

namespace sentinel {

class cyhal_spi_transport;

}

///
/// \brief CYHAL-based SPI master transport implementation
///
/// \details Implements SPI master mode communication using the Cypress Hardware
///          Abstraction Layer. Supports configurable SPI modes (0-3), clock
///          frequencies, and optional software-controlled chip select for
///          devices requiring manual CS control.
///
class sentinel::cyhal_spi_transport
    : public byte_transport<cyhal_spi_transport, spi_tag> {
public:
    using byte_transport<cyhal_spi_transport, spi_tag>::write;
    using byte_transport<cyhal_spi_transport, spi_tag>::read;
    using byte_transport<cyhal_spi_transport, spi_tag>::write_read;
    using byte_transport<cyhal_spi_transport, spi_tag>::transfer;
    using byte_transport<cyhal_spi_transport, spi_tag>::transfer_async;

    ///
    /// \brief Construct SPI transport over an initialized CYHAL SPI object
    ///
    /// \details Chip-select control is not owned by this transport; pair it
    ///          with a \ref sentinel::spi_flash_bus_controller (or manage
    ///          CS at the call site) if software CS is required.
    ///
    /// \param spi_object Pointer to initialized CYHAL SPI object
    ///
    explicit cyhal_spi_transport(cyhal_spi_t *spi_object) noexcept
        : m_spi_object(spi_object) {}

    ///
    /// \brief Configure SPI frequency and mode
    ///
    /// \param hz SPI clock frequency in Hertz
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t configure(uint32_t hz) noexcept {
        auto result = cyhal_spi_set_frequency(m_spi_object, hz);
        return result;
    }

    ///
    /// \brief Write bytes to SPI device
    ///
    /// \param tx Pointer to transmit buffer
    /// \param size Number of bytes to transmit
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t write(const uint8_t *tx, size_t size) noexcept {
        return cyhal_spi_transfer(m_spi_object, tx, size, nullptr, 0, 0xFF);
    }

    ///
    /// \brief Bosch Sensortec API write wrapper (SPI)
    ///
    /// \details Static wrapper function compatible with Bosch Sensortec sensor
    ///          APIs (BME280, BMP388, BMA400, etc.) for SPI mode. For Bosch
    ///          SPI sensors the most-significant bit of the register address
    ///          selects direction (\c 0 for write, \c 1 for read), so this
    ///          wrapper clears it before sending the address followed by the
    ///          payload.
    ///
    /// \param reg_addr Starting register address to write
    /// \param reg_data Pointer to data buffer to write
    /// \param length Number of bytes to write
    /// \param intf_ptr Interface pointer (must point to a
    ///                 \ref cyhal_spi_transport instance)
    /// \return Bosch API compatible result code (0 = success)
    ///
    /// \note Chip-select assert/de-assert is expected to be handled by the
    ///       underlying CYHAL SPI driver or by external CS GPIO management.
    ///
    static int8_t bosch_write(uint8_t reg_addr, const uint8_t *reg_data,
                              uint32_t length, void *intf_ptr) noexcept {
        auto *self = static_cast<cyhal_spi_transport *>(intf_ptr);

        // [reg_addr & 0x7F, data0, data1, ...] in one contiguous SPI transfer
        auto buffer = std::array<uint8_t, 256>{};
        buffer[0] = static_cast<uint8_t>(reg_addr & 0x7F);
        std::copy(reg_data, reg_data + length, buffer.data() + 1);

        return static_cast<int8_t>(self->write(buffer.data(), length + 1));
    }

    ///
    /// \brief Read bytes from SPI device
    ///
    /// \param rx Pointer to receive buffer
    /// \param size Number of bytes to read
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t read(uint8_t *rx, size_t size) noexcept {
        return cyhal_spi_transfer(m_spi_object, nullptr, 0, rx, size, 0xFF);
    }

    ///
    /// \brief Bosch Sensortec API read wrapper (SPI)
    ///
    /// \details Static wrapper function compatible with Bosch Sensortec sensor
    ///          APIs (BME280, BMP388, BMA400, etc.) for SPI mode. Bosch SPI
    ///          reads send the register address with its MSB set, then clock
    ///          out \p length bytes of response.
    ///
    /// \param reg_addr Starting register address to read from
    /// \param reg_data Pointer to buffer for read data
    /// \param length Number of bytes to read
    /// \param intf_ptr Interface pointer (must point to a
    ///                 \ref cyhal_spi_transport instance)
    /// \return Bosch API compatible result code (0 = success)
    ///
    static int8_t bosch_read(uint8_t reg_addr, uint8_t *reg_data,
                             uint32_t length, void *intf_ptr) noexcept {
        auto *self = static_cast<cyhal_spi_transport *>(intf_ptr);
        auto cmd = static_cast<uint8_t>(reg_addr | 0x80);
        return static_cast<int8_t>(
            self->write_read(&cmd, sizeof(cmd), reg_data, length));
    }

    ///
    /// \brief Full-duplex write and read
    ///
    /// \details Performs simultaneous write and read (full-duplex transfer).
    ///          Common for SPI where data is exchanged in both directions
    ///          during the same clock cycles.
    ///
    /// \param tx Pointer to transmit buffer
    /// \param tx_size Number of bytes to transmit
    /// \param rx Pointer to receive buffer
    /// \param rx_size Number of bytes to receive
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t write_read(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                         size_t rx_size) noexcept {
        return cyhal_spi_transfer(m_spi_object, tx, tx_size, rx, rx_size, 0xFF);
    }

    ///
    /// \brief Transfer data (blocking)
    ///
    /// \param tx Pointer to transmit buffer
    /// \param tx_size Number of bytes to transmit
    /// \param rx Pointer to receive buffer
    /// \param rx_size Number of bytes to receive
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t transfer(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                       size_t rx_size) noexcept {
        return cyhal_spi_transfer(m_spi_object, tx, tx_size, rx, rx_size, 0xFF);
    }

    ///
    /// \brief Transfer data (non-blocking)
    ///
    /// \param tx Pointer to transmit buffer
    /// \param tx_size Number of bytes to transmit
    /// \param rx Pointer to receive buffer
    /// \param rx_size Number of bytes to receive
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t transfer_async(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                             size_t rx_size) noexcept {
        return cyhal_spi_transfer_async(m_spi_object, tx, tx_size, rx, rx_size);
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
    /// \brief Bosch Sensortec API delay wrapper (SPI)
    ///
    /// \details Static wrapper function compatible with Bosch Sensortec sensor
    ///          APIs (BME280, BMP388, BMA400, etc.). Bosch callbacks express
    ///          their period in microseconds; this wrapper forwards to the
    ///          CYHAL microsecond delay.
    ///
    /// \param period Delay duration in microseconds
    /// \param intf_ptr Interface pointer (must point to a
    ///                 \ref cyhal_spi_transport instance)
    ///
    /// \note This function signature matches Bosch Sensortec's required
    ///       function pointer type for delay operations (\c void return).
    ///
    static void bosch_delay(uint32_t period, void *intf_ptr) noexcept {
        auto *self = static_cast<cyhal_spi_transport *>(intf_ptr);
        self->delay_us(period);
    }

    ///
    /// \brief True simultaneous full-duplex 3-byte SPI transfer (DRV8308)
    ///
    /// \details Performs a 3-byte full-duplex SPI transfer using raw SCB
    ///          FIFO operations. Required by the DRV8308 motor controller
    ///          which responds with data while the master is still sending
    ///          (true full-duplex, not sequential TX-then-RX).
    ///
    ///          Unlike cyhal_spi_transfer() which may do TX-then-RX
    ///          sequentially, this uses direct SCB register access for
    ///          true simultaneous transfer.
    ///
    /// \param txrx 3-byte buffer for both TX (input) and RX (output)
    ///             On entry: contains bytes to transmit
    ///             On exit: contains bytes received (overwrites TX data)
    /// \return true on success, false on timeout
    ///
    bool transfer_full_duplex_3byte(uint8_t *txrx) noexcept {
        // Clear FIFOs before transfer (like old firmware)
        Cy_SCB_SPI_ClearRxFifo(SpiMaster_HW);
        Cy_SCB_SPI_ClearTxFifo(SpiMaster_HW);

        // Push 3 bytes to TX FIFO (this starts the transfer)
        for (auto i = size_t{}; i < 3; i++) {
            while (Cy_SCB_SPI_GetNumInTxFifo(SpiMaster_HW) >= 64) {
            }
            Cy_SCB_SPI_Write(SpiMaster_HW, txrx[i]);
        }

        // Wait for transfer to complete
        while (!Cy_SCB_SPI_IsTxComplete(SpiMaster_HW)) {
        }

        // Read 3 bytes from RX FIFO (received simultaneously)
        for (auto i = size_t{}; i < 3; i++) {
            auto timeout = uint32_t{10000};

            while (Cy_SCB_SPI_GetNumInRxFifo(SpiMaster_HW) == 0 && --timeout) {
            }

            if (timeout == 0) {
                return false;
            }

            txrx[i] = static_cast<uint8_t>(Cy_SCB_SPI_Read(SpiMaster_HW));
        }

        return true;
    }

private:
    cyhal_spi_t *m_spi_object; ///< Pointer to CYHAL SPI object
};

#endif /* SENTINEL_CYHAL_SPI_TRANSPORT_HPP */
