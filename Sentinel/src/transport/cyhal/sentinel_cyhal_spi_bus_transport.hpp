///
/// \file    sentinel_cyhal_spi_bus_transport.hpp
/// \brief   CYHAL SPI transport that routes through the bus-arbiter task
///
/// \details This header provides \ref sentinel::cyhal_spi_bus_transport,
///          a drop-in replacement for \ref sentinel::cyhal_spi_transport
///          that submits its transactions to a
///          \ref sentinel::task::spi_bus arbiter task instead of calling
///          CYHAL directly. Drivers parameterised over the
///          \c byte_transport CRTP base (\c sentinel::w25q128 in this
///          branch; \c sentinel::bme280 once issue #2 moves it to SPI)
///          work unchanged; switch a driver from direct CYHAL access to
///          bus-arbitrated access by changing the template type at the
///          call site.
///
///          Architectural relationship:
///
///              driver (W25Q128 / BME280-on-SPI / …)
///                │
///                │  templated on a byte_transport<T, spi_tag>
///                ▼
///              cyhal_spi_bus_transport            ← this file
///                │
///                │  submits spi_request via queue
///                ▼
///              sentinel::task::spi_bus            ← the arbiter task
///                │
///                │  cyhal_spi_select_active_ssel + cyhal_spi_transfer
///                ▼
///              cyhal_spi_t  (one physical SCB)
///
///          One transport instance per driver instance (so each driver
///          has its own response queue and SS pin); many transport
///          instances per bus (they all funnel into the same arbiter
///          task and use distinct SS pins via the SCB's SS0..SS3
///          routing).
///
/// \author  galudino
/// \date    2026-05-18
/// \version 1.0 - Initial skeleton
///

#ifndef SENTINEL_CYHAL_SPI_BUS_TRANSPORT_HPP
#define SENTINEL_CYHAL_SPI_BUS_TRANSPORT_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_result.h"
#include "cyhal_gpio.h"
#include "cyhal_system.h"
#include "portmacro.h"
#include "queue.h"
}
#pragma GCC diagnostic pop

#include "sentinel_byte_transport.hpp"
#include "sentinel_span.hpp"
#include "sentinel_task_spi_bus.hpp"
#include "sentinel_utilities.hpp"

#include <cstddef>
#include <cstdint>

namespace sentinel {

class cyhal_spi_bus_transport;

} // namespace sentinel

///
/// \brief Bus-arbitrated CYHAL SPI transport.
///
/// \details Implements the \c byte_transport<_, spi_tag> CRTP interface
///          on top of a shared \ref sentinel::task::spi_bus arbiter.
///          Each instance owns a dedicated FreeRTOS response queue plus
///          the slave-select pin identifier for its target device.
///
class sentinel::cyhal_spi_bus_transport
    : public byte_transport<cyhal_spi_bus_transport, spi_tag> {
public:
    ///
    /// \brief Construct a bus-arbitrated SPI transport.
    ///
    /// \param bus  Reference to the bus arbiter that owns the underlying
    ///             CYHAL SPI handle.
    /// \param ssel \c cyhal_gpio_t of the slave-select pin assigned to
    ///             this device. Must have been routed as one of the
    ///             SCB's SS0..SS3 lines in Device Configurator (e.g.,
    ///             \c CYBSP_SPI_CS for the first slave on the bus).
    ///
    explicit cyhal_spi_bus_transport(sentinel::task::spi_bus &bus,
                                     cyhal_gpio_t ssel) noexcept
        : m_bus(bus), m_ssel(ssel),
          m_response_queue(
              xQueueCreate(1, sizeof(sentinel::task::spi_response))) {}

    /// \brief Destroy the transport, deleting its response queue.
    ~cyhal_spi_bus_transport() noexcept {
        if (m_response_queue != nullptr) {
            vQueueDelete(m_response_queue);
        }
    }

    // Non-copyable.
    cyhal_spi_bus_transport(const cyhal_spi_bus_transport &) = delete;
    cyhal_spi_bus_transport &
    operator=(const cyhal_spi_bus_transport &) = delete;

    ///
    /// \brief Move-construct, transferring ownership of the response queue.
    /// \param other Source instance; left with a null response queue.
    ///
    cyhal_spi_bus_transport(cyhal_spi_bus_transport &&other) noexcept
        : m_bus(other.m_bus), m_ssel(other.m_ssel),
          m_response_queue(other.m_response_queue) {
        other.m_response_queue = nullptr;
    }
    cyhal_spi_bus_transport &
    operator=(cyhal_spi_bus_transport &&) = delete; // m_bus is a reference

    // =====================================================================
    // Configuration
    // =====================================================================

    ///
    /// \brief Configure bus frequency.
    ///
    /// \details The bus arbiter task owns the CYHAL handle; per-request
    ///          reconfiguration would race with in-flight transactions.
    ///          Bus speed is set once during \c peripheral_initialize via
    ///          the Device Configurator. This method is a no-op that
    ///          returns success so the \c byte_transport interface
    ///          contract is satisfied.
    ///
    /// \param hz Ignored.
    /// \return \c CY_RSLT_SUCCESS.
    ///
    cy_rslt_t configure(uint32_t hz) noexcept {
        sentinel::unused(hz);
        return CY_RSLT_SUCCESS;
    }

    // =====================================================================
    // Pointer-based primitives
    // =====================================================================

    ///
    /// \brief Write \p size bytes (blocking).
    /// \param tx   Pointer to transmit buffer.
    /// \param size Number of bytes to transmit.
    /// \return \c cy_rslt_t forwarded from the arbiter's response.
    ///
    cy_rslt_t write(const uint8_t *tx, size_t size) noexcept {
        auto request = sentinel::task::spi_request{};
        request.ssel = m_ssel;
        request.tx = sentinel::make_cspan(tx, size);
        request.rx = sentinel::span<uint8_t>{};
        request.write_fill = sentinel::task::spi_bus::DEFAULT_WRITE_FILL;
        request.response_queue = m_response_queue;
        return exchange(request);
    }

    ///
    /// \brief Read \p size bytes (blocking).
    /// \param rx   Pointer to receive buffer.
    /// \param size Number of bytes to read.
    /// \return \c cy_rslt_t forwarded from the arbiter's response.
    ///
    cy_rslt_t read(uint8_t *rx, size_t size) noexcept {
        auto request = sentinel::task::spi_request{};
        request.ssel = m_ssel;
        request.tx = sentinel::span<const uint8_t>{};
        request.rx = sentinel::make_span(rx, size);
        request.write_fill = sentinel::task::spi_bus::DEFAULT_WRITE_FILL;
        request.response_queue = m_response_queue;
        return exchange(request);
    }

    ///
    /// \brief Full-duplex write/read (blocking).
    /// \param tx      Pointer to transmit buffer.
    /// \param tx_size Number of bytes to transmit.
    /// \param rx      Pointer to receive buffer.
    /// \param rx_size Number of bytes to receive.
    /// \return \c cy_rslt_t forwarded from the arbiter's response.
    ///
    cy_rslt_t write_read(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                         size_t rx_size) noexcept {
        auto request = sentinel::task::spi_request{};
        request.ssel = m_ssel;
        request.tx = sentinel::make_cspan(tx, tx_size);
        request.rx = sentinel::make_span(rx, rx_size);
        request.write_fill = sentinel::task::spi_bus::DEFAULT_WRITE_FILL;
        request.response_queue = m_response_queue;
        return exchange(request);
    }

    ///
    /// \brief Full-duplex transfer (blocking).
    ///
    /// \details Same shape as \c write_read above — both functions
    ///          ultimately submit a single \c spi_request to the arbiter
    ///          which calls \c cyhal_spi_transfer. CYHAL handles the
    ///          tx/rx length asymmetry by padding the shorter buffer
    ///          with \c write_fill or discarding extra received bytes.
    ///
    /// \param tx      Pointer to transmit buffer.
    /// \param tx_size Number of bytes to transmit.
    /// \param rx      Pointer to receive buffer.
    /// \param rx_size Number of bytes to receive.
    /// \return \c cy_rslt_t forwarded from the arbiter's response.
    ///
    cy_rslt_t transfer(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                       size_t rx_size) noexcept {
        return write_read(tx, tx_size, rx, rx_size);
    }

    ///
    /// \brief Non-blocking transfer (not supported by the bus arbiter).
    ///
    /// \details The arbiter processes one request at a time synchronously;
    ///          there is no async path through it. Returns
    ///          \c CY_RSLT_TYPE_ERROR. Drop down to
    ///          \ref sentinel::task::spi_bus::submit and poll your
    ///          response queue yourself if you need async.
    ///
    /// \param tx      Unused (accepted for interface compatibility).
    /// \param tx_size Unused (accepted for interface compatibility).
    /// \param rx      Unused (accepted for interface compatibility).
    /// \param rx_size Unused (accepted for interface compatibility).
    /// \return Always \c CY_RSLT_TYPE_ERROR.
    ///
    cy_rslt_t transfer_async(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                             size_t rx_size) noexcept {
        sentinel::unused(tx);
        sentinel::unused(tx_size);
        sentinel::unused(rx);
        sentinel::unused(rx_size);
        return CY_RSLT_TYPE_ERROR;
    }

    // =====================================================================
    // Delay
    // =====================================================================

    ///
    /// \brief Delay execution (task sleep, not busy-wait).
    /// \param milliseconds Delay duration in milliseconds.
    /// \return \c CY_RSLT_SUCCESS (always succeeds).
    ///
    cy_rslt_t delay(uint32_t milliseconds) noexcept {
        vTaskDelay(pdMS_TO_TICKS(milliseconds));
        return CY_RSLT_SUCCESS;
    }

    ///
    /// \brief Delay execution (microseconds, busy-wait via CYHAL).
    /// \param microseconds Delay duration in microseconds.
    /// \return \c CY_RSLT_SUCCESS (always succeeds).
    ///
    cy_rslt_t delay_us(uint32_t microseconds) noexcept {
        cyhal_system_delay_us(microseconds);
        return CY_RSLT_SUCCESS;
    }

private:
    ///
    /// \brief Submit a request directly to the bus and return the raw
    ///        CYHAL status from the response.
    ///
    /// \param request Fully populated SPI request (SS pin, tx/rx spans,
    ///                write-fill byte, and this transport's response queue).
    /// \return Raw \c cy_rslt_t from the arbiter's response, or
    ///         \c CY_RSLT_TYPE_ERROR if submit fails or the response
    ///         never arrives.
    ///
    cy_rslt_t exchange(const sentinel::task::spi_request &request) noexcept {
        if (m_bus.submit(request) != pdPASS) {
            return static_cast<cy_rslt_t>(CY_RSLT_TYPE_ERROR);
        }

        auto response = sentinel::task::spi_response{};
        if (xQueueReceive(m_response_queue, &response, portMAX_DELAY) !=
            pdPASS) {
            return static_cast<cy_rslt_t>(CY_RSLT_TYPE_ERROR);
        }

        return response.cy_status;
    }

    sentinel::task::spi_bus &m_bus; ///< Bus arbiter (non-owning).
    cyhal_gpio_t m_ssel;            ///< SS pin for this device.
    QueueHandle_t m_response_queue; ///< Per-instance response.
};

#endif /* SENTINEL_CYHAL_SPI_BUS_TRANSPORT_HPP */
