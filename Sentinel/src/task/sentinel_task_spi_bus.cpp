///
/// \file    sentinel_task_spi_bus.cpp
/// \brief   FreeRTOS SPI bus arbiter task — implementation
///
/// \details Implements \ref sentinel::task::spi_bus declared in
///          \c sentinel_task_spi_bus.hpp. The task owns one CYHAL SPI
///          handle, receives \ref sentinel::task::spi_request objects on
///          a queue, switches the SCB's active slave-select line for each
///          request, runs the transfer with bounded retries, and posts
///          the \ref sentinel::task::spi_response back to the requester.
///
/// \author  galudino
/// \date    2026-05-18
/// \version 1.0 - Initial skeleton implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_result.h"
#include "cyhal_gpio.h"
#include "cyhal_spi.h"
#include "portmacro.h"
#include "queue.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_task_spi_bus.hpp"

#include <cstdint>

namespace sentinel::task {

// ============================================================================
// task_create
// ============================================================================

BaseType_t spi_bus::task_create(UBaseType_t priority, uint16_t stack_words,
                                UBaseType_t queue_length) noexcept {
    // Idempotency: refuse a second task_create call so a wiring mistake
    // can be spotted at startup instead of leaking queues and tasks.
    if (m_request_queue != nullptr || m_task_handle != nullptr) {
        return pdFAIL;
    }

    m_request_queue = xQueueCreate(queue_length, sizeof(spi_request));
    if (m_request_queue == nullptr) {
        return pdFAIL;
    }

    auto rc = xTaskCreate(&spi_bus::task_trampoline, m_task_name, stack_words,
                          this, priority, &m_task_handle);
    if (rc != pdPASS) {
        vQueueDelete(m_request_queue);
        m_request_queue = nullptr;
        m_task_handle = nullptr;
        return rc;
    }

    return pdPASS;
}

// ============================================================================
// submit / transact
// ============================================================================

BaseType_t spi_bus::submit(const spi_request &request,
                           TickType_t timeout) noexcept {
    if (m_request_queue == nullptr) {
        return errQUEUE_FULL;
    }
    return xQueueSendToBack(m_request_queue, &request, timeout);
}

bool spi_bus::transact(const spi_request &request,
                       TickType_t request_submit_timeout,
                       TickType_t response_wait_timeout) noexcept {
    if (request.response_queue == nullptr) {
        return false;
    }

    if (submit(request, request_submit_timeout) != pdPASS) {
        return false;
    }

    auto response = spi_response{};
    if (xQueueReceive(request.response_queue, &response,
                      response_wait_timeout) != pdPASS) {
        return false;
    }

    return response.success;
}

// ============================================================================
// task entry-point + main loop
// ============================================================================

void spi_bus::task_trampoline(void *arg) {
    auto *self = static_cast<spi_bus *>(arg);
    self->run();
}

[[noreturn]] void spi_bus::run() {
    m_up_and_running = true;

    while (true) {
        auto request = spi_request{};

        if (xQueueReceive(m_request_queue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }

        auto response = process(request);

        if (request.response_queue != nullptr) {
            constexpr auto post_timeout_ticks = TickType_t{pdMS_TO_TICKS(10)};
            xQueueSendToBack(request.response_queue, &response,
                             post_timeout_ticks);
        }
    }
}

// ============================================================================
// process — single-request execution with retry
// ============================================================================

spi_response spi_bus::process(const spi_request &request) noexcept {
    auto response = spi_response{};
    response.success = false;
    response.cy_status = cy_en_rslt_type_t::CY_RSLT_TYPE_ERROR;

    auto const has_tx = !request.tx.empty();
    auto const has_rx = !request.rx.empty();

    // No-op request.
    if (!has_tx && !has_rx) {
        response.success = true;
        response.cy_status = CY_RSLT_SUCCESS;
        return response;
    }

    // Route this transaction to the requested slave-select line. The SCB
    // hardware will assert this SS pin at the start of the next transfer
    // and deassert it at the end automatically.
    auto select_status =
        cyhal_spi_select_active_ssel(m_spi_object, request.ssel);
    if (select_status != CY_RSLT_SUCCESS) {
        response.cy_status = select_status;
        return response;
    }

    for (auto attempt = uint8_t{0}; attempt < INNER_RETRIES; ++attempt) {
        // cyhal_spi_transfer handles all four shapes uniformly:
        //   tx + rx     → full-duplex max(tx_len, rx_len) bytes
        //   tx + no rx  → write-only
        //   no tx + rx  → read-only (write_fill clocked on MOSI)
        //   no tx + no rx → caught above as a no-op
        auto const *tx_ptr = has_tx ? request.tx.data() : nullptr;
        auto tx_size = has_tx ? request.tx.size() : size_t{0};
        auto *rx_ptr = has_rx ? request.rx.data() : nullptr;
        auto rx_size = has_rx ? request.rx.size() : size_t{0};

        auto status = cyhal_spi_transfer(m_spi_object, tx_ptr, tx_size, rx_ptr,
                                         rx_size, request.write_fill);

        response.cy_status = status;

        if (status == CY_RSLT_SUCCESS) {
            response.success = true;
            return response;
        }

        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
    }

    // All inner retries exhausted. The cy_status field carries the last
    // error for the caller to inspect.
    return response;
}

} // namespace sentinel::task
