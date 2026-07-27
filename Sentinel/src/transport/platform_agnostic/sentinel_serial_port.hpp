///
/// \file    sentinel_serial_port.hpp
/// \brief   Platform-agnostic UART/serial port interface using CRTP
///
/// \details Provides a platform-independent façade for serial communication
///          using the Curiously Recurring Template Pattern (CRTP). The façade
///          forwards to a platform-specific implementation without virtual
///          overhead. Pointer overloads are convenience shims that forward to
///          span-based forms.
///
/// \author  galudino
/// \date    2021–2024
/// \version 1.1 - Fix byte helpers to use façade, const-correct available()
///

#ifndef SENTINEL_SERIAL_PORT_HPP
#define SENTINEL_SERIAL_PORT_HPP

#include "sentinel_span.hpp"

#include <cstddef>
#include <cstdint>

namespace sentinel {

///
/// \ingroup transport
/// \brief Tag for UART transport type (modeled separately via serial_port)
///
struct uart_tag {};

///
/// \brief Parity mode
///
enum class parity_mode : uint8_t { none, even, odd };

///
/// \brief Stop bits
///
enum class stop_bits_mode : uint8_t { one, two };

///
/// \brief Flow control
///
enum class flow_control_mode : uint8_t { none, rts_cts };

///
/// \brief Serial port configuration
///
struct serial_config {
    uint32_t baud{115200}; ///< Baud rate
    uint8_t data_bits{8};  ///< Data bits (5..9, impl-defined)

    parity_mode parity{parity_mode::none};                   ///< Parity mode
    stop_bits_mode stop_bits{stop_bits_mode::one};           ///< Stop bits
    flow_control_mode flow_control{flow_control_mode::none}; ///< Flow control
};

///
/// \brief Platform-agnostic serial-port façade (CRTP).
///
/// \tparam Implementation Platform-specific implementation (CRTP derived)
///                        providing the same member functions (e.g.
///                        \c sentinel::cyhal_uart_port).
///
template <typename Implementation>
class serial_port {
public:
    // ---------------------------------------------------------------------
    // Configuration
    // ---------------------------------------------------------------------

    ///
    /// \brief Configure serial port parameters
    ///
    /// \param config Baud rate, data bits, parity, stop bits, flow control.
    /// \return Implementation-specific status/result code.
    ///
    auto configure(const serial_config &config) noexcept {
        return impl().configure(config);
    }

    ///
    /// \brief Set baud rate only
    ///
    /// \param baud_rate New baud rate in bits per second.
    /// \return Implementation-specific status/result code.
    ///
    auto set_baud_rate(uint32_t baud_rate) noexcept {
        return impl().set_baud_rate(baud_rate);
    }

    // ---------------------------------------------------------------------
    // Write
    // ---------------------------------------------------------------------

    ///
    /// \brief Write bytes (span)
    ///
    /// \param tx Span of bytes to transmit.
    /// \return Implementation-specific status/result code.
    ///
    auto write(span<const uint8_t> tx) noexcept { return impl().write(tx); }

    ///
    /// \brief Write bytes (pointer convenience)
    ///
    /// \param tx      Pointer to transmit buffer.
    /// \param tx_size Number of bytes to transmit.
    /// \return Implementation-specific status/result code.
    ///
    auto write(const uint8_t *tx, size_t tx_size) noexcept {
        return impl().write(make_cspan(tx, tx_size));
    }

    ///
    /// \brief Write a single byte
    ///
    /// \details Routes through the façade to use the span-based
    ///          implementation.
    ///
    /// \param byte Byte value to write.
    /// \return Implementation-specific status/result code.
    ///
    auto write_byte(uint8_t byte) noexcept {
        return this->write(&byte, sizeof(byte));
    }

    // ---------------------------------------------------------------------
    // Read
    // ---------------------------------------------------------------------

    ///
    /// \brief Read bytes with timeout (span)
    ///
    /// \param rx         Span for receive buffer.
    /// \param timeout_ms Timeout in milliseconds.
    /// \return Implementation-specific status/result code.
    ///
    auto read(span<uint8_t> rx, uint32_t timeout_ms) noexcept {
        return impl().read(rx, timeout_ms);
    }

    ///
    /// \brief Read bytes with timeout (pointer convenience)
    ///
    /// \param rx         Pointer to receive buffer.
    /// \param rx_size    Number of bytes to read.
    /// \param timeout_ms Timeout in milliseconds.
    /// \return Implementation-specific status/result code.
    ///
    auto read(uint8_t *rx, size_t rx_size, uint32_t timeout_ms) noexcept {
        return impl().read(make_span(rx, rx_size), timeout_ms);
    }

    ///
    /// \brief Read a single byte with timeout
    ///
    /// \details Routes through the façade to use the span-based
    ///          implementation.
    ///
    /// \param byte       Pointer to receive the byte read.
    /// \param timeout_ms Timeout in milliseconds.
    /// \return Implementation-specific status/result code.
    ///
    auto read_byte(uint8_t *byte, uint32_t timeout_ms) noexcept {
        return this->read(byte, sizeof(*byte), timeout_ms);
    }

    // ---------------------------------------------------------------------
    // Buffer utilities
    // ---------------------------------------------------------------------

    ///
    /// \brief Bytes available to read (implementation-defined meaning)
    /// \return Number of bytes available, per the underlying implementation.
    ///
    auto available() const noexcept { return impl().available(); }

    ///
    /// \brief Wait for transmit drain/idle
    /// \return Implementation-specific status/result code.
    ///
    auto flush_tx() noexcept { return impl().flush_tx(); }

    ///
    /// \brief Clear receive buffer (discard unread bytes)
    /// \return Implementation-specific status/result code.
    ///
    auto clear_rx() noexcept { return impl().clear_rx(); }

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

    // ---------------------------------------------------------------------
    // Higher-level helpers
    // ---------------------------------------------------------------------

    ///
    /// \brief Read until terminal condition (pointer convenience)
    ///
    /// \tparam Predicate Callable taking a \c uint8_t byte and returning
    ///                   \c bool (true when the byte satisfies the
    ///                   terminal condition).
    /// \param rx         Pointer to receive buffer.
    /// \param end        Capacity of \p rx in bytes.
    /// \param is_terminal Predicate tested against each byte read.
    /// \param timeout_ms Overall timeout in milliseconds.
    /// \return Number of bytes read into \p rx.
    ///
    template <typename Predicate>
    size_t read_until(uint8_t *rx, size_t end, Predicate is_terminal,
                      uint32_t timeout_ms) noexcept {
        return read_until(make_span(rx, end), is_terminal, timeout_ms);
    }

    ///
    /// \brief Read until terminal condition (span)
    ///
    /// \details Repeatedly attempts to read 1 byte chunks (with a small
    ///          internal step delay) until the predicate returns true for the
    ///          last byte, the buffer fills, or the timeout elapses.
    ///
    /// \tparam Predicate Callable taking a \c uint8_t byte and returning
    ///                   \c bool (true when the byte satisfies the
    ///                   terminal condition).
    /// \param rx         Span for receive buffer.
    /// \param is_terminal Predicate tested against each byte read.
    /// \param timeout_ms Overall timeout in milliseconds.
    /// \return Number of bytes read into \p rx.
    ///
    template <typename Predicate>
    size_t read_until(span<uint8_t> rx, Predicate is_terminal,
                      uint32_t timeout_ms) noexcept {
        auto n = size_t{};
        auto elapsed = uint32_t{};
        const auto step = uint32_t{5};

        while (n < rx.size() && elapsed <= timeout_ms) {
            // read at most 1 byte this step
            n += impl().read(rx.subspan(n, 1), step);

            if (n && is_terminal(rx[n - 1])) {
                break;
            }

            elapsed += step;
        }

        return n;
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
    const Implementation &impl() const noexcept {
        return static_cast<const Implementation &>(*this);
    }
};

} // namespace sentinel

#endif /* SENTINEL_SERIAL_PORT_HPP */
