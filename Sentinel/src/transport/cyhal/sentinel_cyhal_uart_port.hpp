///
/// \file    sentinel_cyhal_uart_port.hpp
/// \brief   CYHAL (Cypress HAL) UART transport implementation
///
/// \details Implements the \ref sentinel::serial_port façade for PSoC 6 UART
/// using
///          CYHAL. Reception is interrupt-driven into a single-producer/single-
///          consumer ring buffer (ISR producer, foreground consumer).
///          Provides convenience helpers for non-blocking drains, “read exact”
///          with timeout, and “write all or error”.
///
/// \author  galudino
/// \date    2021–2024
/// \version 1.2 - IRQ prio arg, error counting, helpers
/// (read_some/read_exact/write_all),
///                finer read() polling step, style/consistency updates
///

#ifndef SENTINEL_CYHAL_UART_PORT_HPP
#define SENTINEL_CYHAL_UART_PORT_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cyhal_hw_types.h"
#include "cyhal_system.h"
#include "cyhal_uart.h"
}
#pragma GCC diagnostic pop

#include "sentinel_ring_buffer.hpp"
#include "sentinel_serial_port.hpp"
#include "sentinel_span.hpp"

#include <cstdint>

namespace sentinel {

///
/// \ingroup transport
/// \brief CYHAL-based UART transport with interrupt-driven RX
///
/// \details Incoming bytes are pushed from the UART ISR into a ring buffer.
///          Foreground code drains the buffer via \c read()/\c read_some().
///          The ring is used in SPSC mode (ISR producer, foreground consumer),
///          so no atomics are required.
///
template <size_t RXBufferCapacity = 256, bool OverwriteOldestOnFull = true>
class cyhal_uart_port : public serial_port<cyhal_uart_port<RXBufferCapacity>> {
public:
    ///
    /// \brief Construct with an initialized CYHAL UART object
    ///
    /// \param uart_object Pointer to initialized CYHAL UART instance
    /// \param irq_prio    NVIC priority to use for RX events (default 3)
    ///
    explicit cyhal_uart_port(cyhal_uart_t *uart_object,
                             uint8_t irq_prio = 3) noexcept
        : m_uart_object(uart_object) {
        // Register C-style callback trampoline
        cyhal_uart_register_callback(
            m_uart_object, &cyhal_uart_port::interrupt_request_callback, this);

        // Enable RX events at requested priority
        cyhal_uart_enable_event(
            m_uart_object,
            static_cast<cyhal_uart_event_t>(
                cyhal_uart_event_t::CYHAL_UART_IRQ_RX_NOT_EMPTY |
                cyhal_uart_event_t::CYHAL_UART_IRQ_RX_DONE |
                cyhal_uart_event_t::CYHAL_UART_IRQ_RX_ERROR),
            3 /* prio */, true);
    }

    // ---------------------------------------------------------------------
    // Configuration
    // ---------------------------------------------------------------------

    ///
    /// \brief Configure UART parameters (baud, data bits, parity, stop bits,
    /// flow control)
    ///
    /// \details Flow control (\c sentinel::flow_control_mode::rts_cts) requires
    ///          RTS/CTS pins to be assigned in the Device Configurator or via
    ///          your board init. If your project enables/disables RTS/CTS at
    ///          runtime, call the appropriate HAL API here where marked.
    ///
    /// \param config Serial configuration
    /// \return cy_rslt_t \c CY_RSLT_SUCCESS on success, else HAL error
    ///
    cy_rslt_t configure(const serial_config &config) noexcept {
        m_config = config;

        auto result = cyhal_uart_set_baud(m_uart_object, config.baud,
                                          &m_actual_baud_rate);
        if (result != CY_RSLT_SUCCESS) {
            return result;
        }

        auto hal_config = cyhal_uart_cfg_t{};
        hal_config.data_bits = config.data_bits;
        hal_config.parity =
            (config.parity == parity_mode::none)
                ? cyhal_uart_parity_t::CYHAL_UART_PARITY_NONE
                : ((config.parity == parity_mode::even)
                       ? cyhal_uart_parity_t::CYHAL_UART_PARITY_EVEN
                       : cyhal_uart_parity_t::CYHAL_UART_PARITY_ODD);
        hal_config.stop_bits =
            (config.stop_bits == stop_bits_mode::one) ? 1 : 2;
        hal_config.rx_buffer = nullptr; // we use our own RX ring
        hal_config.rx_buffer_size = 0;

        result = cyhal_uart_configure(m_uart_object, &hal_config);

        if (result != CY_RSLT_SUCCESS) {
            return result;
        }

        // Flow control policy:
        // If using RTS/CTS, pins must already be assigned in the Device
        // Configurator (or set via your board init). If your HAL exposes a
        // runtime API to enable hardware flow control, this is the place to
        // call it.
        //
        // Example (pseudo):
        // if (m_config.flow_control == flow_control_mode::rts_cts) {
        //     (void)cyhal_uart_enable_flow_control(m_uart_object, true, true);
        // }

        return CY_RSLT_SUCCESS;
    }

    ///
    /// \brief Set baud rate only
    ///
    /// \param baud_rate Desired baud
    /// \return cy_rslt_t
    ///
    cy_rslt_t set_baud_rate(uint32_t baud_rate) noexcept {
        m_config.baud = baud_rate;
        return cyhal_uart_set_baud(m_uart_object, baud_rate,
                                   &m_actual_baud_rate);
    }

    // ---------------------------------------------------------------------
    // Write
    // ---------------------------------------------------------------------

    ///
    /// \brief Write bytes (blocking best-effort)
    ///
    /// \details Attempts to send all bytes, returning the number actually sent.
    ///
    /// \param src Span of bytes to transmit
    /// \return bytes written
    ///
    size_t write(span<const uint8_t> src) noexcept {
        auto sent = size_t{0};

        while (sent < src.size()) {
            auto chunk = src.size() - sent; // HAL may reduce this
            auto result = cyhal_uart_write(
                m_uart_object, const_cast<uint8_t *>(src.data() + sent),
                &chunk);

            if (result != CY_RSLT_SUCCESS || chunk == 0) {
                break;
            }

            sent += chunk;
        }
        return sent;
    }

    ///
    /// \brief Write all or return error
    ///
    /// \param src Span of bytes to transmit
    /// \return cy_rslt_t
    ///
    cy_rslt_t write_all(span<const uint8_t> src) noexcept {
        auto sent = size_t{0};

        while (sent < src.size()) {
            auto chunk = src.size() - sent;
            auto result = cyhal_uart_write(
                m_uart_object, const_cast<uint8_t *>(src.data() + sent),
                &chunk);

            if (result != CY_RSLT_SUCCESS || chunk == 0) {
                return (result != CY_RSLT_SUCCESS)
                           ? result
                           : to_underlying(CY_RSLT_TYPE_ERROR);
            }

            sent += chunk;
        }
        return CY_RSLT_SUCCESS;
    }

    // ---------------------------------------------------------------------
    // Read
    // ---------------------------------------------------------------------

    ///
    /// \brief Read bytes with timeout (drains ring buffer)
    ///
    /// \param dst         Destination span
    /// \param timeout_ms  Timeout in milliseconds
    /// \return bytes read
    ///
    size_t read(span<uint8_t> dst, uint32_t timeout_ms) noexcept {
        auto got = size_t{0};
        auto elapsed = uint32_t{0};

        const auto step_ms = uint32_t{2};

        while (got < dst.size() && elapsed <= timeout_ms) {
            auto byte = uint8_t{0};

            while (got < dst.size() && m_rx.try_pop(byte)) {
                dst[got++] = byte;
            }

            if (got == dst.size()) {
                break;
            }

            delay(step_ms);
            elapsed += step_ms;
        }
        return got;
    }

    ///
    /// \brief Non-blocking drain of available bytes
    ///
    /// \param dst Destination span
    /// \return bytes read (no sleeping)
    ///
    size_t read_some(span<uint8_t> dst) noexcept {
        auto n = size_t{0};
        auto b = uint8_t{0};

        while (n < dst.size() && m_rx.try_pop(b)) {
            dst[n++] = b;
        }
        return n;
    }

    ///
    /// \brief Read exactly dst.size() or until timeout
    ///
    /// \param dst        Destination span
    /// \param timeout_ms Timeout in milliseconds
    /// \return bytes read (== dst.size() on success)
    ///
    size_t read_exact(span<uint8_t> dst, uint32_t timeout_ms) noexcept {
        auto got = size_t{0};
        auto elapsed = uint32_t{0};

        const auto step_ms = uint32_t{2};

        while (got < dst.size() && elapsed <= timeout_ms) {
            got += this->read(dst.subspan(got), step_ms);

            if (got == dst.size()) {
                break;
            }

            elapsed += step_ms;
        }
        return got;
    }

    // ---------------------------------------------------------------------
    // Buffer / status
    // ---------------------------------------------------------------------

    ///
    /// \brief Bytes currently available in the RX ring
    /// \return Number of bytes currently buffered and ready to read.
    ///
    size_t available() const noexcept { return m_rx.size(); }

    ///
    /// \brief Wait for TX to become idle
    /// \return \c cy_rslt_t (\c CY_RSLT_SUCCESS on success).
    ///
    cy_rslt_t flush_tx() noexcept {
        // TODO: check if this block actually works?
        // if not, we need to try something else.
        while (cyhal_uart_is_tx_active(m_uart_object)) {
            // block
        }
        return CY_RSLT_SUCCESS;
    }

    ///
    /// \brief Discard all unread bytes
    ///
    void clear_rx() noexcept { m_rx.clear(); }

    ///
    /// \brief Number of RX error events observed since construction
    /// \return Cumulative count of RX error events.
    ///
    uint32_t rx_error_count() const noexcept { return m_rx_error_count; }

    // ---------------------------------------------------------------------
    // Delay
    // ---------------------------------------------------------------------

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

private:
    // ---------------------------------------------------------------------
    // IRQ plumbing
    // ---------------------------------------------------------------------

    ///
    /// \brief C callback trampoline
    ///
    /// \param arg   Opaque context; the \c cyhal_uart_port instance that
    ///              registered this callback.
    /// \param event Bitmask of CYHAL UART events that fired.
    ///
    static void interrupt_request_callback(void *arg,
                                           cyhal_uart_event_t event) noexcept {
        static_cast<cyhal_uart_port *>(arg)->on_irq(event);
    }

    ///
    /// \brief UART ISR handler
    ///
    /// \details Pushes all readable bytes into the ring buffer. On overflow,
    ///          either overwrites oldest or drops new (policy is the template
    ///          parameter \c OverwriteOldestOnFull).
    ///
    /// \param event Bitmask of CYHAL UART events that fired.
    ///
    void on_irq(cyhal_uart_event_t event) noexcept {
        if ((event & cyhal_uart_event_t::CYHAL_UART_IRQ_RX_ERROR) != 0) {
            ++m_rx_error_count;
            // If your HAL requires explicit error flag clear, do it here.
            // e.g., cyhal_uart_clear(...);
        }

        if ((event & (cyhal_uart_event_t::CYHAL_UART_IRQ_RX_NOT_EMPTY |
                      cyhal_uart_event_t::CYHAL_UART_IRQ_RX_DONE)) != 0) {
            auto byte = uint8_t{0};

            while (cyhal_uart_readable(m_uart_object)) {
                if (cyhal_uart_getc(m_uart_object, &byte, 1000) !=
                    CY_RSLT_SUCCESS) {
                    break;
                }

                if constexpr (OverwriteOldestOnFull) {
                    m_rx.push_overwrite(byte);
                } else {
                    m_rx.try_push(byte);
                }
            }
        }
    }

private:
    // Note: m_rx is SPSC: ISR producer, foreground consumer.
    cyhal_uart_t *m_uart_object; ///< Non-owning CYHAL UART handle.

    serial_config m_config{};       ///< Last-applied port configuration.
    uint32_t m_actual_baud_rate{0}; ///< Baud rate actually achieved by HAL.

    sentinel::ring_buffer_<uint8_t, RXBufferCapacity> m_rx{}; ///< RX ring
                                                              ///< (SPSC).
    uint32_t m_rx_error_count{0}; ///< Count of RX error events since ctor.
};

} // namespace sentinel

#endif /* SENTINEL_CYHAL_UART_PORT_HPP */
