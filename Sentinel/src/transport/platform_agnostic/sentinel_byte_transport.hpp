///
/// \file    sentinel_byte_transport.hpp
/// \brief   Platform-agnostic I2C/SPI transport interface using CRTP
/// (tag-specialized)
///
/// \details This header provides a platform-independent interface for
///          byte-based communication protocols using the Curiously Recurring
///          Template Pattern (CRTP). The interface is **specialized per
///          protocol tag** (I2C or SPI) so each protocol exposes only the
///          operations that make sense for it:
///            - I2C: target addressing, timeouts, and STOP/repeated-start.
///            - SPI: full-duplex transfers and optional CS helpers.
///          This avoids ambiguous method sets and removes the need for SFINAE
///          in a single monolithic base.
///
/// \author  galudino
/// \date    2021-2026
/// \version 1.2 - SPI façade gains delay_us for Bosch-style sensor drivers
///

#ifndef SENTINEL_BYTE_TRANSPORT_HPP
#define SENTINEL_BYTE_TRANSPORT_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_result.h"
}
#pragma GCC diagnostic pop

#include "sentinel_span.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace sentinel {

///
/// \defgroup transport Transports
/// \brief Platform-agnostic byte-based communication interfaces
///
/// These CRTP façades define common interfaces for I2C and SPI communication.
/// Platform-specific implementations (e.g. CYHAL) inherit from these
/// and provide the concrete HAL-backed functionality.
///

// ============================================================================
// Transport tags
// ============================================================================

///
/// \ingroup transport
/// \brief Tag for I2C transport type
///
struct i2c_tag {};

///
/// \ingroup transport
/// \brief Tag for SPI transport type
///
struct spi_tag {};

///
/// \ingroup transport
/// \brief Primary template declaration for byte transport façade
///
/// \details Protocol-specific specializations are provided for:
///          - \ref sentinel::i2c_tag : I²C-style transfers
///          - \ref sentinel::spi_tag : SPI-style transfers
///
/// \tparam Implementation Platform-specific implementation (CRTP derived)
/// \tparam Tag  Protocol tag (\ref i2c_tag or \ref spi_tag)
///
template <typename Implementation, typename Tag>
class byte_transport;

} // namespace sentinel

// ============================================================================
// I2C specialization
// ============================================================================

namespace sentinel {

///
/// \ingroup transport
/// \brief Platform-agnostic I2C transport façade (CRTP)
///
/// \details Exposes I²C-specific operations, including target addressing,
///          STOP control, and repeated-start sequences. All methods forward to
///          the derived CRTP implementation without virtual overhead.
///
/// \tparam Implementation Platform-specific I²C implementation deriving from
///              \c byte_transport<Implementation, i2c_tag>.
///
/// \par Example
/// \code
/// sentinel::cyhal_i2c_transport bus(&main_i2c, 0x20);
/// uint8_t tx[]{0x10};
/// uint8_t rx[4]{};
/// bus.write_read(tx, rx, 0, 0, false, true); // repeated-start read
/// \endcode
///
template <typename Implementation>
class byte_transport<Implementation, i2c_tag> {
public:
    // ---------------------------------------------------------------------
    // Addressing
    // ---------------------------------------------------------------------

    ///
    /// \brief Get current I2C target device address
    ///
    /// \return Current I2C target address (implementation-defined width)
    ///
    auto target_address() const noexcept { return impl().target_address(); }

    ///
    /// \brief Set I2C target device address
    ///
    /// \param addr New I2C target address (7- or 10-bit, impl-dependent)
    /// \return Implementation-specific status/result code
    ///
    auto set_target_address(uint16_t addr) noexcept {
        return impl().set_target_address(addr);
    }

    // ---------------------------------------------------------------------
    // Configuration
    // ---------------------------------------------------------------------

    ///
    /// \brief Configure I2C bus frequency
    ///
    /// \param frequency_hz I2C clock in Hertz (e.g., 100000 or 400000)
    /// \return Implementation-specific status/result code
    ///
    auto configure(uint32_t frequency_hz) noexcept {
        return impl().configure(frequency_hz);
    }

    // ---------------------------------------------------------------------
    // Basic I/O (pointer-based)
    // ---------------------------------------------------------------------

    ///
    /// \brief Write bytes to the I2C target
    ///
    /// \param tx         Pointer to transmit buffer
    /// \param size          Number of bytes to transmit
    /// \param timeout_ms Timeout in milliseconds (0 for no timeout)
    /// \param send_stop  \c true to send STOP, \c false to hold the bus
    /// \return Implementation-specific status/result code
    ///
    auto write(const uint8_t *tx, size_t size, uint32_t timeout_ms = 0,
               bool send_stop = true) noexcept {
        return impl().write(tx, size, timeout_ms, send_stop);
    }

    ///
    /// \brief Read bytes from the I2C target
    ///
    /// \param rx         Pointer to receive buffer
    /// \param size          Number of bytes to read
    /// \param timeout_ms Timeout in milliseconds (0 for no timeout)
    /// \param send_stop  \c true to send STOP after read
    /// \return Implementation-specific status/result code
    ///
    auto read(uint8_t *rx, size_t size, uint32_t timeout_ms = 0,
              bool send_stop = true) noexcept {
        return impl().read(rx, size, timeout_ms, send_stop);
    }

    ///
    /// \brief Bosch Sensortec API write wrapper
    ///
    /// \details Static wrapper function compatible with Bosch Sensortec sensor
    ///          APIs (BMA400, BMP388, etc.). Forwards write operations to the
    ///          implementation's I2C write method.
    ///
    /// \param reg_addr Starting register address to write
    /// \param reg_data Pointer to data buffer to write
    /// \param length Number of bytes to write
    /// \param intf_ptr Interface pointer (typically points to transport
    /// instance)
    /// \return Bosch API compatible result code (0 = success)
    ///
    /// \note This function signature matches Bosch Sensortec's required
    ///       function pointer type for write operations.
    ///
    static auto bosch_write(uint8_t reg_addr, const uint8_t *reg_data,
                            uint32_t length, void *intf_ptr) noexcept {
        return impl().write(reg_addr, reg_data, length, intf_ptr);
    }

    ///
    /// \brief Bosch Sensortec API read wrapper
    ///
    /// \details Static wrapper function compatible with Bosch Sensortec sensor
    ///          APIs (BMA400, BMP388, etc.). Forwards read operations to the
    ///          implementation's I2C read method.
    ///
    /// \param reg_addr Starting register address to read from
    /// \param reg_data Pointer to buffer for read data
    /// \param length Number of bytes to read
    /// \param intf_ptr Interface pointer (typically points to transport
    /// instance)
    /// \return Bosch API compatible result code (0 = success)
    ///
    /// \note This function signature matches Bosch Sensortec's required
    ///       function pointer type for read operations.
    ///
    static auto bosch_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length,
                           void *intf_ptr) noexcept {
        return impl().read(reg_addr, reg_data, length, intf_ptr);
    }

    // ---------------------------------------------------------------------
    // Basic I/O (span convenience; forwards to pointer-based)
    // ---------------------------------------------------------------------

    ///
    /// \brief Write bytes to the I2C target (span convenience)
    ///
    /// \param tx         Span of bytes to transmit
    /// \param timeout_ms Timeout in milliseconds (0 for no timeout)
    /// \param send_stop  \c true to send STOP, \c false to hold the bus
    /// \return Implementation-specific status/result code
    ///
    auto write(span<const uint8_t> tx, uint32_t timeout_ms = 0,
               bool send_stop = true) noexcept {
        return write(tx.data(), tx.size(), timeout_ms, send_stop);
    }

    ///
    /// \brief Read bytes from the I2C target (span convenience)
    ///
    /// \param rx         Span for receive buffer
    /// \param timeout_ms Timeout in milliseconds (0 for no timeout)
    /// \param send_stop  \c true to send STOP after read
    /// \return Implementation-specific status/result code
    ///
    auto read(span<uint8_t> rx, uint32_t timeout_ms = 0,
              bool send_stop = true) noexcept {
        return read(rx.data(), rx.size(), timeout_ms, send_stop);
    }

    // ---------------------------------------------------------------------
    // Repeated-start / combined transactions (pointer-based)
    // ---------------------------------------------------------------------

    ///
    /// \brief Write then read with optional repeated-start
    ///
    /// \details Performs a write phase followed by a read phase. For I2C,
    ///          this typically maps to a repeated-start when
    ///          \p send_stop_on_write is \c false.
    ///
    /// \param tx                  Pointer to transmit buffer
    /// \param tx_size                 Number of bytes to transmit
    /// \param rx                  Pointer to receive buffer
    /// \param rx_size                 Number of bytes to receive
    /// \param timeout_on_write    Timeout for write phase (ms)
    /// \param timeout_on_read     Timeout for read phase (ms)
    /// \param send_stop_on_write  Send STOP after write (usually \c false)
    /// \param send_stop_on_read   Send STOP after read (usually \c true)
    /// \return Implementation-specific status/result code
    ///
    auto write_read(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                    size_t rx_size, uint32_t timeout_on_write = 0,
                    uint32_t timeout_on_read = 0,
                    bool send_stop_on_write = false,
                    bool send_stop_on_read = true) noexcept {
        return impl().write_read(tx, tx_size, rx, rx_size, timeout_on_write,
                                 timeout_on_read, send_stop_on_write,
                                 send_stop_on_read);
    }

    ///
    /// \brief Write then read with optional repeated-start (span convenience)
    ///
    /// \param tx                  Span of bytes to transmit
    /// \param rx                  Span for receive buffer
    /// \param timeout_on_write    Timeout for write phase (ms)
    /// \param timeout_on_read     Timeout for read phase (ms)
    /// \param send_stop_on_write  Send STOP after write
    /// \param send_stop_on_read   Send STOP after read
    /// \return Implementation-specific status/result code
    ///
    auto write_read(span<const uint8_t> tx, span<uint8_t> rx,
                    uint32_t timeout_on_write = 0, uint32_t timeout_on_read = 0,
                    bool send_stop_on_write = false,
                    bool send_stop_on_read = true) noexcept {
        return write_read(tx.data(), tx.size(), rx.data(), rx.size(),
                          timeout_on_write, timeout_on_read, send_stop_on_write,
                          send_stop_on_read);
    }

    // ---------------------------------------------------------------------
    // Optional async combined transfer
    // ---------------------------------------------------------------------

    ///
    /// \brief Non-blocking combined transfer (implementation-defined)
    ///
    /// \param tx Span of bytes to transmit
    /// \param rx Span for receive buffer
    /// \return Implementation-specific status/result code
    ///
    auto transfer_async(span<const uint8_t> tx, span<uint8_t> rx) noexcept {
        return impl().transfer_async(tx, rx);
    }

    ///
    /// \brief Non-blocking combined transfer (pointer convenience)
    ///
    /// \param tx Pointer to transmit buffer
    /// \param tx_size Number of bytes to transmit
    /// \param rx Pointer to receive buffer
    /// \param rx_size Number of bytes to receive
    /// \return Implementation-specific status/result code
    ///
    auto transfer_async(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                        size_t rx_size) noexcept {
        return transfer_async(make_cspan(tx, tx_size), make_span(rx, rx_size));
    }

    // ---------------------------------------------------------------------
    // Delay
    // ---------------------------------------------------------------------

    ///
    /// \brief Delay execution
    ///
    /// \param milliseconds Delay duration in milliseconds
    /// \return Implementation-specific status/result code
    ///
    auto delay(uint32_t milliseconds) noexcept {
        return impl().delay(milliseconds);
    }

    ///
    /// \brief Delay execution (microseconds)
    ///
    /// \param microseconds Delay duration in microseconds
    /// \return Implementation-specific status/result code
    ///
    auto delay_us(uint32_t microseconds) noexcept {
        return impl().delay_us(microseconds);
    }

    ///
    /// \brief Bosch Sensortec API delay wrapper
    ///
    /// \details Static wrapper function compatible with Bosch Sensortec sensor
    ///          APIs (BMA400, BMP388, etc.). Forwards delay operations to the
    ///          implementation's delay method.
    ///
    /// \param period Delay duration in milliseconds
    /// \param intf_ptr Interface pointer (typically points to transport
    /// instance)
    /// \return Bosch API compatible result code (0 = success)
    ///
    /// \note This function signature matches Bosch Sensortec's required
    ///       function pointer type for delay operations.
    ///
    static auto bosch_delay(uint32_t period, void *intf_ptr) noexcept {
        return impl().delay(period, intf_ptr);
    }

private:
    ///
    /// \brief Get reference to derived implementation
    ///
    /// \return Reference to implementation
    ///
    Implementation &impl() noexcept {
        return static_cast<Implementation &>(*this);
    }

    ///
    /// \brief Get const reference to derived implementation
    ///
    /// \return Const reference to implementation
    ///
    Implementation const &impl() const noexcept {
        return static_cast<Implementation const &>(*this);
    }
};

} // namespace sentinel

// ============================================================================
// SPI specialization
// ============================================================================

namespace sentinel {

///
/// \ingroup transport
/// \brief Platform-agnostic SPI transport façade (CRTP)
///
/// \details Exposes SPI-specific operations: full-duplex transfers and, if
///          provided by the implementation, software-controlled chip select
///          helpers. SPI has no notion of STOP or addressing; the API is
///          focused on synchronous block transfers.
///
/// \tparam Implementation Platform-specific SPI implementation deriving from
///              \c byte_transport<Implementation, spi_tag>.
///
template <typename Implementation>
class byte_transport<Implementation, spi_tag> {
public:
    // ---------------------------------------------------------------------
    // Configuration
    // ---------------------------------------------------------------------

    ///
    /// \brief Configure SPI bus
    ///
    /// \param hz   SPI clock in Hertz
    /// \return Implementation-specific status/result code
    ///
    auto configure(uint32_t hz) noexcept { return impl().configure(hz); }

    // ---------------------------------------------------------------------
    // Transfers (pointer-based)
    // ---------------------------------------------------------------------

    ///
    /// \brief Write bytes (blocking)
    ///
    /// \param tx Pointer to transmit buffer
    /// \param size  Number of bytes to transmit
    /// \return Implementation-specific status/result code
    ///
    auto write(const uint8_t *tx, size_t size) noexcept {
        return impl().write(tx, size);
    }

    ///
    /// \brief Read bytes (blocking)
    ///
    /// \param rx Pointer to receive buffer
    /// \param size  Number of bytes to read
    /// \return Implementation-specific status/result code
    ///
    auto read(uint8_t *rx, size_t size) noexcept {
        return impl().read(rx, size);
    }

    ///
    /// \brief Full-duplex write/read (blocking)
    ///
    /// \param tx  Pointer to transmit buffer
    /// \param tx_size Number of bytes to transmit
    /// \param rx  Pointer to receive buffer
    /// \param rx_size Number of bytes to receive
    /// \return Implementation-specific status/result code
    ///
    auto write_read(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                    size_t rx_size) noexcept {
        return impl().write_read(tx, tx_size, rx, rx_size);
    }

    ///
    /// \brief Combined transfer (blocking)
    ///
    /// \param tx  Pointer to transmit buffer
    /// \param tx_size Number of bytes to transmit
    /// \param rx  Pointer to receive buffer
    /// \param rx_size Number of bytes to receive
    /// \return Implementation-specific status/result code
    ///
    auto transfer(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                  size_t rx_size) noexcept {
        return impl().transfer(tx, tx_size, rx, rx_size);
    }

    ///
    /// \brief Combined transfer (non-blocking)
    ///
    /// \param tx  Pointer to transmit buffer
    /// \param tx_size Number of bytes to transmit
    /// \param rx  Pointer to receive buffer
    /// \param rx_size Number of bytes to receive
    /// \return Implementation-specific status/result code
    ///
    auto transfer_async(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                        size_t rx_size) noexcept {
        return impl().transfer_async(tx, tx_size, rx, rx_size);
    }

    // ---------------------------------------------------------------------
    // Transfers (span convenience; forward to pointer-based)
    // ---------------------------------------------------------------------

    ///
    /// \brief Write bytes (span convenience)
    ///
    /// \param tx Span of bytes to transmit
    /// \return Implementation-specific status/result code
    ///
    auto write(span<const uint8_t> tx) noexcept {
        return write(tx.data(), tx.size());
    }

    ///
    /// \brief Read bytes (span convenience)
    ///
    /// \param rx Span for receive buffer
    /// \return Implementation-specific status/result code
    ///
    auto read(span<uint8_t> rx) noexcept { return read(rx.data(), rx.size()); }

    ///
    /// \brief Full-duplex write/read (span convenience)
    ///
    /// \param tx Span of bytes to transmit
    /// \param rx Span for receive buffer
    /// \return Implementation-specific status/result code
    ///
    auto write_read(span<const uint8_t> tx, span<uint8_t> rx) noexcept {
        return write_read(tx.data(), tx.size(), rx.data(), rx.size());
    }

    ///
    /// \brief Combined transfer (span convenience)
    ///
    /// \param tx Span of bytes to transmit
    /// \param rx Span for receive buffer
    /// \return Implementation-specific status/result code
    ///
    auto transfer(span<const uint8_t> tx, span<uint8_t> rx) noexcept {
        return transfer(tx.data(), tx.size(), rx.data(), rx.size());
    }

    ///
    /// \brief Combined transfer (non-blocking; span convenience)
    ///
    /// \param tx Span of bytes to transmit
    /// \param rx Span for receive buffer
    /// \return Implementation-specific status/result code
    ///
    auto transfer_async(span<const uint8_t> tx, span<uint8_t> rx) noexcept {
        return transfer_async(tx.data(), tx.size(), rx.data(), rx.size());
    }

    // ---------------------------------------------------------------------
    // Delay
    // ---------------------------------------------------------------------

    ///
    /// \brief Delay execution
    ///
    /// \param milliseconds Delay duration in milliseconds
    /// \return Implementation-specific status/result code
    ///
    auto delay(uint32_t milliseconds) noexcept {
        return impl().delay(milliseconds);
    }

    ///
    /// \brief Delay execution (microseconds)
    ///
    /// \details Required for Bosch Sensortec drivers whose delay callback
    ///          signature is expressed in microseconds.
    ///
    /// \param microseconds Delay duration in microseconds
    /// \return Implementation-specific status/result code
    ///
    auto delay_us(uint32_t microseconds) noexcept {
        return impl().delay_us(microseconds);
    }

private:
    ///
    /// \brief Get reference to derived implementation
    ///
    /// \return Reference to implementation
    ///
    Implementation &impl() noexcept {
        return static_cast<Implementation &>(*this);
    }

    ///
    /// \brief Get const reference to derived implementation
    ///
    /// \return Const reference to implementation
    ///
    Implementation const &impl() const noexcept {
        return static_cast<Implementation const &>(*this);
    }
};

} // namespace sentinel

#endif /* SENTINEL_BYTE_TRANSPORT_HPP */
