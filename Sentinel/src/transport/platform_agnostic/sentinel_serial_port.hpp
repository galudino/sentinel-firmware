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

template <typename Implementation>
class serial_port {
public:
    // ---------------------------------------------------------------------
    // Configuration
    // ---------------------------------------------------------------------

    ///
    /// \brief Configure serial port parameters
    ///
    auto configure(const serial_config &config) noexcept {
        return impl().configure(config);
    }

    ///
    /// \brief Set baud rate only
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
    auto write(span<const uint8_t> tx) noexcept { return impl().write(tx); }

    ///
    /// \brief Write bytes (pointer convenience)
    ///
    auto write(const uint8_t *tx, size_t tx_size) noexcept {
        return impl().write(make_cspan(tx, tx_size));
    }

    ///
    /// \brief Write a single byte
    ///
    /// \note Routes through the façade to use the span-based implementation.
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
    auto read(span<uint8_t> rx, uint32_t timeout_ms) noexcept {
        return impl().read(rx, timeout_ms);
    }

    ///
    /// \brief Read bytes with timeout (pointer convenience)
    ///
    auto read(uint8_t *rx, size_t rx_size, uint32_t timeout_ms) noexcept {
        return impl().read(make_span(rx, rx_size), timeout_ms);
    }

    ///
    /// \brief Read a single byte with timeout
    ///
    /// \note Routes through the façade to use the span-based implementation.
    ///
    auto read_byte(uint8_t *byte, uint32_t timeout_ms) noexcept {
        return this->read(byte, sizeof(*byte), timeout_ms);
    }

    // ---------------------------------------------------------------------
    // Buffer utilities
    // ---------------------------------------------------------------------

    ///
    /// \brief Bytes available to read (implementation-defined meaning)
    ///
    auto available() const noexcept { return impl().available(); }

    ///
    /// \brief Wait for transmit drain/idle
    ///
    auto flush_tx() noexcept { return impl().flush_tx(); }

    ///
    /// \brief Clear receive buffer (discard unread bytes)
    ///
    auto clear_rx() noexcept { return impl().clear_rx(); }

    // ---------------------------------------------------------------------
    // Delay
    // ---------------------------------------------------------------------

    ///
    /// \brief Delay in milliseconds
    ///
    /// \param milliseconds Delay in milliseconds
    /// \return Implementation-specific status/result code
    ///
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
    /// \brief Delay in microseconds
    ///
    /// \param milliseconds Delay in microseconds
    /// \return Implementation-specific status/result code
    ///
    ///
    /// \brief Delay execution
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
