///
/// \file    sentinel_cyhal_i2c_bus_transport.hpp
/// \brief   CYHAL I²C transport that routes through the bus-arbiter task
///
/// \details This header provides \ref sentinel::cyhal_i2c_bus_transport,
///          a drop-in replacement for \ref sentinel::cyhal_i2c_transport
///          that submits its transactions to a
///          \ref sentinel::task::i2c_bus arbiter task instead of calling
///          CYHAL directly. Drivers parameterised over the
///          \c byte_transport CRTP base
///          (\c sentinel::bme280, \c sentinel::ds3231, …) work unchanged;
///          switch a driver from direct CYHAL access to bus-arbitrated
///          access by changing the template type at the call site.
///
///          Architectural relationship:
///
///              driver (BME280 / DS3231 / …)
///                │
///                │  templated on a byte_transport<T, i2c_tag>
///                ▼
///              cyhal_i2c_bus_transport            ← this file
///                │
///                │  submits i2c_request via queue
///                ▼
///              sentinel::task::i2c_bus            ← the arbiter task
///                │
///                │  cyhal_i2c_master_write / _read
///                ▼
///              cyhal_i2c_t  (one physical SCB)
///
///          One transport instance per driver instance (so each driver
///          has its own response queue and target address); many
///          transport instances per bus (they all funnel into the same
///          arbiter task).
///
/// \author  galudino
/// \date    2026-05-17
/// \version 1.0 - Initial skeleton
///

#ifndef SENTINEL_CYHAL_I2C_BUS_TRANSPORT_HPP
#define SENTINEL_CYHAL_I2C_BUS_TRANSPORT_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_result.h"
#include "cyhal_system.h"
#include "portmacro.h"
#include "queue.h"
}
#pragma GCC diagnostic pop

#include "sentinel_byte_transport.hpp"
#include "sentinel_span.hpp"
#include "sentinel_task_i2c_bus.hpp"
#include "sentinel_utilities.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace sentinel {

class cyhal_i2c_bus_transport;

} // namespace sentinel

///
/// \brief Bus-arbitrated CYHAL I²C transport.
///
/// \details Implements the \c byte_transport<_, i2c_tag> CRTP interface
///          on top of a shared \ref sentinel::task::i2c_bus arbiter.
///          Each instance owns a dedicated FreeRTOS response queue so
///          that responses for *this* transport's transactions cannot
///          be confused with another transport's responses sharing the
///          same bus.
///
class sentinel::cyhal_i2c_bus_transport
    : public byte_transport<cyhal_i2c_bus_transport, i2c_tag> {
public:
    ///
    /// \brief Construct a bus-arbitrated I²C transport.
    ///
    /// \details The transport creates a one-slot FreeRTOS response queue
    ///          for itself in this constructor. The bus arbiter
    ///          (\p bus) must have been initialised already, but does
    ///          not need to have its task spawned yet — \ref task_create
    ///          on the bus can happen later as long as it happens
    ///          before any I²C transaction is attempted through this
    ///          transport.
    ///
    /// \param bus             Reference to the bus arbiter that owns
    ///                        the underlying CYHAL handle.
    /// \param target_address  7-bit I²C address of the chip this
    ///                        transport talks to.
    ///
    explicit cyhal_i2c_bus_transport(sentinel::task::i2c_bus &bus,
                                     uint16_t target_address) noexcept
        : m_bus(bus), m_target_address(target_address),
          m_response_queue(xQueueCreate(1, sizeof(sentinel::task::i2c_response))) {}

    ~cyhal_i2c_bus_transport() noexcept {
        if (m_response_queue != nullptr) {
            vQueueDelete(m_response_queue);
        }
    }

    // Non-copyable.
    cyhal_i2c_bus_transport(const cyhal_i2c_bus_transport &) = delete;
    cyhal_i2c_bus_transport &
    operator=(const cyhal_i2c_bus_transport &) = delete;

    // Movable: transfer ownership of the response queue.
    cyhal_i2c_bus_transport(cyhal_i2c_bus_transport &&other) noexcept
        : m_bus(other.m_bus), m_target_address(other.m_target_address),
          m_response_queue(other.m_response_queue) {
        other.m_response_queue = nullptr;
    }
    cyhal_i2c_bus_transport &
    operator=(cyhal_i2c_bus_transport &&) = delete; // m_bus is a reference

    // =====================================================================
    // Addressing
    // =====================================================================

    uint16_t target_address() const noexcept { return m_target_address; }

    cy_rslt_t set_target_address(uint16_t addr) noexcept {
        m_target_address = addr;
        return CY_RSLT_SUCCESS;
    }

    ///
    /// \brief Configure bus frequency.
    ///
    /// \details The bus arbiter task owns the CYHAL handle; reconfiguring
    ///          the bus from arbitrary requester threads would race with
    ///          in-flight transactions. For phase 1 the bus is configured
    ///          once during peripheral init via the Device Configurator;
    ///          this method is a no-op that returns success so the
    ///          \c byte_transport interface contract is satisfied.
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
    /// \brief Write \p size bytes to the target.
    ///
    /// \details \p send_stop is accepted for API compatibility with
    ///          \c cyhal_i2c_transport but is always treated as \c true
    ///          here: a stand-alone write-only request closes with STOP.
    ///          For repeated-start writes followed by a read, use
    ///          \ref write_read instead.
    ///
    cy_rslt_t write(const uint8_t *tx, size_t size, uint32_t timeout_ms = 0,
                    bool send_stop = true) noexcept {
        sentinel::unused(send_stop);

        auto request                  = sentinel::task::i2c_request{};
        request.target_address        = m_target_address;
        request.tx                    = sentinel::make_cspan(tx, size);
        request.rx                    = sentinel::span<uint8_t>{};
        request.timeout_ms_per_phase  = timeout_ms;
        request.response_queue        = m_response_queue;

        return exchange(request);
    }

    ///
    /// \brief Read \p size bytes from the target.
    ///
    /// \details \p send_stop is accepted for API compatibility and always
    ///          treated as \c true.
    ///
    cy_rslt_t read(uint8_t *rx, size_t size, uint32_t timeout_ms = 0,
                   bool send_stop = true) noexcept {
        sentinel::unused(send_stop);

        auto request                  = sentinel::task::i2c_request{};
        request.target_address        = m_target_address;
        request.tx                    = sentinel::span<const uint8_t>{};
        request.rx                    = sentinel::make_span(rx, size);
        request.timeout_ms_per_phase  = timeout_ms;
        request.response_queue        = m_response_queue;

        return exchange(request);
    }

    ///
    /// \brief Write then read with repeated start.
    ///
    /// \details Standard register-read pattern. \p send_stop_on_write,
    ///          \p send_stop_on_read, and the per-phase timeouts are
    ///          accepted for API compatibility with
    ///          \c cyhal_i2c_transport; the arbiter always issues
    ///          repeated-start (no STOP between write and read) and
    ///          STOP after the read, and uses
    ///          \p timeout_on_write as the single per-phase timeout.
    ///
    cy_rslt_t write_read(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                         size_t rx_size, uint32_t timeout_on_write = 0,
                         uint32_t timeout_on_read = 0,
                         bool send_stop_on_write = false,
                         bool send_stop_on_read = true) noexcept {
        sentinel::unused(timeout_on_read);
        sentinel::unused(send_stop_on_write);
        sentinel::unused(send_stop_on_read);

        auto request                  = sentinel::task::i2c_request{};
        request.target_address        = m_target_address;
        request.tx                    = sentinel::make_cspan(tx, tx_size);
        request.rx                    = sentinel::make_span(rx, rx_size);
        request.timeout_ms_per_phase  = timeout_on_write;
        request.response_queue        = m_response_queue;

        return exchange(request);
    }

    ///
    /// \brief Non-blocking transfer (not supported by the bus arbiter).
    ///
    /// \details The arbiter task synchronously processes one request at
    ///          a time; there is no async path through it. Returns
    ///          \c CY_RSLT_TYPE_ERROR. If async is needed, send the
    ///          request via \ref sentinel::task::i2c_bus::submit and
    ///          poll your response queue yourself.
    ///
    cy_rslt_t transfer_async(sentinel::span<const uint8_t> tx,
                             sentinel::span<uint8_t> rx) noexcept {
        sentinel::unused(tx);
        sentinel::unused(rx);
        return CY_RSLT_TYPE_ERROR;
    }

    // =====================================================================
    // Delay
    // =====================================================================

    cy_rslt_t delay(uint32_t milliseconds) noexcept {
        vTaskDelay(pdMS_TO_TICKS(milliseconds));
        return CY_RSLT_SUCCESS;
    }

    cy_rslt_t delay_us(uint32_t microseconds) noexcept {
        cyhal_system_delay_us(microseconds);
        return CY_RSLT_SUCCESS;
    }

    // =====================================================================
    // Bosch Sensortec API wrappers (same shape as cyhal_i2c_transport)
    // =====================================================================

    ///
    /// \brief Bosch Sensortec read callback compatible with the Bosch C
    ///        driver function-pointer ABI.
    ///
    /// \details Routes through this transport's bus arbiter; \p intf_ptr
    ///          must point to a \ref cyhal_i2c_bus_transport instance.
    ///
    static int8_t bosch_read(uint8_t reg_addr, uint8_t *reg_data,
                             uint32_t length, void *intf_ptr) noexcept {
        auto *self = static_cast<cyhal_i2c_bus_transport *>(intf_ptr);
        auto rc    = self->write_read(&reg_addr, sizeof(reg_addr), reg_data,
                                      length, /*timeout_on_write=*/100,
                                      /*timeout_on_read=*/100,
                                      /*send_stop_on_write=*/false,
                                      /*send_stop_on_read=*/true);
        return rc == CY_RSLT_SUCCESS ? int8_t{0} : int8_t{-1};
    }

    ///
    /// \brief Bosch Sensortec write callback compatible with the Bosch C
    ///        driver function-pointer ABI.
    ///
    /// \details Combines \p reg_addr and \p reg_data into a single
    ///          contiguous transmit buffer and submits a write-only
    ///          request. The internal scratch buffer is sized at
    ///          256 bytes — comfortably above any Bosch driver's per-
    ///          call write payload, but a static assertion in the
    ///          implementation guards against the unlikely future case
    ///          of a larger transfer.
    ///
    static int8_t bosch_write(uint8_t reg_addr, const uint8_t *reg_data,
                              uint32_t length, void *intf_ptr) noexcept {
        auto *self = static_cast<cyhal_i2c_bus_transport *>(intf_ptr);

        auto buffer = std::array<uint8_t, 256>{};
        if (length + 1 > buffer.size()) {
            return -1;
        }
        buffer[0] = reg_addr;
        std::copy(reg_data, reg_data + length, buffer.data() + 1);

        auto rc = self->write(buffer.data(), length + 1,
                              /*timeout_ms=*/100, /*send_stop=*/true);
        return rc == CY_RSLT_SUCCESS ? int8_t{0} : int8_t{-1};
    }

    ///
    /// \brief Bosch Sensortec delay callback compatible with the Bosch C
    ///        driver function-pointer ABI.
    ///
    /// \details Bosch callbacks express their period in microseconds.
    ///          The arbiter task is not involved — delays do not require
    ///          bus access — so this forwards directly to the CYHAL
    ///          microsecond delay.
    ///
    static void bosch_delay(uint32_t period, void *intf_ptr) noexcept {
        sentinel::unused(intf_ptr);
        cyhal_system_delay_us(period);
    }

private:
    ///
    /// \brief Submit a request directly to the bus and return the raw
    ///        CYHAL status from the response.
    ///
    /// \details Lower-level than \ref sentinel::task::i2c_bus::transact:
    ///          we manage the submit + receive pair ourselves so the
    ///          underlying \c cy_rslt_t can be returned verbatim, instead
    ///          of being collapsed into a synthetic success/error code.
    ///          That preserves diagnostic information for callers that
    ///          inspect the result (e.g. the BME280 / DS3231 drivers
    ///          stash it in \c last_error).
    ///
    cy_rslt_t exchange(const sentinel::task::i2c_request &request) noexcept {
        // If our response queue failed to allocate (heap exhausted at ctor
        // time), blocking on it would hang forever — fail fast instead.
        if (m_response_queue == nullptr) {
            return static_cast<cy_rslt_t>(CY_RSLT_TYPE_ERROR);
        }
        if (m_bus.submit(request) != pdPASS) {
            return static_cast<cy_rslt_t>(CY_RSLT_TYPE_ERROR);
        }

        auto response = sentinel::task::i2c_response{};
        if (xQueueReceive(m_response_queue, &response, portMAX_DELAY)
            != pdPASS) {
            return static_cast<cy_rslt_t>(CY_RSLT_TYPE_ERROR);
        }

        return response.cy_status;
    }

    sentinel::task::i2c_bus &m_bus;             ///< Bus arbiter (non-owning).
    uint16_t                 m_target_address;  ///< I²C target address.
    QueueHandle_t            m_response_queue;  ///< Per-instance response.
};

#endif /* SENTINEL_CYHAL_I2C_BUS_TRANSPORT_HPP */
