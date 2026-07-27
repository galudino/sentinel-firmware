///
/// \file    sentinel_task_i2c_bus.cpp
/// \brief   FreeRTOS I²C bus arbiter task — implementation
///
/// \details Implements \ref sentinel::task::i2c_bus declared in
///          \c sentinel_task_i2c_bus.hpp. The task owns one CYHAL I²C
///          handle, receives \ref sentinel::task::i2c_request objects on
///          a queue, executes each as a CYHAL write / read /
///          repeated-start sequence with bounded retries, and posts the
///          \ref sentinel::task::i2c_response back to the requester's
///          response queue.
///
/// \author  galudino
/// \date    2026-05-17
/// \version 1.0 - Initial skeleton implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_result.h"
#include "cyhal_i2c.h"
#include "portmacro.h"
#include "queue.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_task_i2c_bus.hpp"
#include "sentinel_utilities.hpp"

#include <cstdint>

namespace sentinel::task {

// ============================================================================
// task_create
// ============================================================================

BaseType_t i2c_bus::task_create(UBaseType_t priority, uint16_t stack_words,
                                UBaseType_t queue_length) noexcept {
    // Idempotency: a second task_create call would leak the prior queue
    // and task. Return pdFAIL so the caller can spot a wiring mistake.
    if (m_request_queue != nullptr || m_task_handle != nullptr) {
        return pdFAIL;
    }

    m_request_queue = xQueueCreate(queue_length, sizeof(i2c_request));
    if (m_request_queue == nullptr) {
        return pdFAIL;
    }

    auto rc = xTaskCreate(&i2c_bus::task_trampoline, m_task_name, stack_words,
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

BaseType_t i2c_bus::submit(const i2c_request &request,
                           TickType_t timeout) noexcept {
    if (m_request_queue == nullptr) {
        return errQUEUE_FULL;
    }
    return xQueueSendToBack(m_request_queue, &request, timeout);
}

bool i2c_bus::transact(const i2c_request &request,
                       TickType_t request_submit_timeout,
                       TickType_t response_wait_timeout) noexcept {
    if (request.response_queue == nullptr) {
        // transact() requires a response queue to block on; the caller
        // either meant submit() or built the request incorrectly.
        return false;
    }

    if (submit(request, request_submit_timeout) != pdPASS) {
        return false;
    }

    auto response = i2c_response{};
    if (xQueueReceive(request.response_queue, &response,
                      response_wait_timeout) != pdPASS) {
        return false;
    }

    return response.success;
}

// ============================================================================
// task entry-point + main loop
// ============================================================================

void i2c_bus::task_trampoline(void *arg) {
    auto *self = static_cast<i2c_bus *>(arg);
    self->run();
}

[[noreturn]] void i2c_bus::run() {
    m_up_and_running = true;

    while (true) {
        auto request = i2c_request{};

        // Block indefinitely until a request arrives. The arbiter has no
        // other work to do; spinning would waste CPU other tasks need.
        if (xQueueReceive(m_request_queue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }

        auto response = process(request);

        // Post the response if the requester provided a return queue.
        // Fire-and-forget requesters set this to nullptr and the response
        // is discarded.
        if (request.response_queue != nullptr) {
            // Use a short timeout so a wedged requester (whose response
            // queue is full and never being drained) does not stall the
            // arbiter for everyone else. If this fails, the requester is
            // already broken; the next transaction proceeds.
            constexpr auto post_timeout_ticks = TickType_t{pdMS_TO_TICKS(10)};
            xQueueSendToBack(request.response_queue, &response,
                             post_timeout_ticks);
        }
    }
}

// ============================================================================
// process — single-request execution with retry
// ============================================================================

i2c_response i2c_bus::process(const i2c_request &request) noexcept {
    auto response = i2c_response{};
    response.success = false;
    response.cy_status = CY_RSLT_TYPE_ERROR;

    auto const has_tx = !request.tx.empty();
    auto const has_rx = !request.rx.empty();

    // No-op request: nothing to send, nothing to receive.
    if (!has_tx && !has_rx) {
        response.success = true;
        response.cy_status = CY_RSLT_SUCCESS;
        return response;
    }

    for (auto attempt = uint8_t{0}; attempt < INNER_RETRIES; attempt++) {
        auto status = cy_rslt_t{CY_RSLT_TYPE_ERROR};

        if (has_tx && has_rx) {
            // Write-then-read with repeated start. The CYHAL write call
            // releases the bus on completion if its \c send_stop argument
            // is \c true; for the repeated-start pattern, pass \c false
            // on the write and let the read close the transaction.
            status = cyhal_i2c_master_write(
                m_i2c_object, request.target_address, request.tx.data(),
                request.tx.size(), request.timeout_ms_per_phase,
                /*send_stop=*/false);

            if (status == CY_RSLT_SUCCESS) {
                status = cyhal_i2c_master_read(
                    m_i2c_object, request.target_address, request.rx.data(),
                    request.rx.size(), request.timeout_ms_per_phase,
                    /*send_stop=*/true);
            }
        } else if (has_tx) {
            // Write-only.
            status = cyhal_i2c_master_write(
                m_i2c_object, request.target_address, request.tx.data(),
                request.tx.size(), request.timeout_ms_per_phase,
                /*send_stop=*/true);
        } else /* has_rx */ {
            // Read-only.
            status = cyhal_i2c_master_read(m_i2c_object, request.target_address,
                                           request.rx.data(), request.rx.size(),
                                           request.timeout_ms_per_phase,
                                           /*send_stop=*/true);
        }

        response.cy_status = status;

        if (status == CY_RSLT_SUCCESS) {
            response.success = true;
            return response;
        }

        // Transient failure: brief delay before the next attempt. The
        // delay also serves as a yield point so higher-priority tasks
        // can run during a noisy-bus stretch.
        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
    }

    // All inner retries exhausted. SCB-level recovery (disable + re-init
    // the peripheral) is the next step to add when we see hangs in the
    // field; for the skeleton, report the failure and let the caller
    // decide how to react. The cy_status field carries the last error.
    return response;
}

} // namespace sentinel::task
