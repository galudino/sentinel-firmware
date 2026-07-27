///
/// \file    sentinel_task_i2c_bus.hpp
/// \brief   FreeRTOS I²C bus arbiter task (request/response queue model)
///
/// \details This header declares \ref sentinel::task::i2c_bus, a dedicated
///          FreeRTOS task that serves as the single owner of one CYHAL I²C
///          peripheral handle (\c cyhal_i2c_t*). Every other task in the
///          system that needs to transact on that bus does so by submitting
///          an \ref sentinel::task::i2c_request to the bus's request queue and
///          blocking on its own response queue. The bus task processes one
///          request at a time, manages retries and bus-level recovery, and
///          posts an \ref sentinel::task::i2c_response back to the requester.
///
///          This model is functionally equivalent to a mutex protecting
///          \c cyhal_i2c_master_write* calls, but it also provides:
///          - a single point in the codebase that handles transient bus
///            errors (NACK retries, brief delays between attempts)
///          - a single point that can perform SCB-level recovery if the
///            bus wedges (planned; not yet implemented in the skeleton)
///          - decoupling: callers never touch the CYHAL handle directly
///
///          One \ref sentinel::task::i2c_bus instance per physical I²C SCB. For
///          the current CYBLE-416045-EVAL hardware that is one instance over
///          \c sentinel::resource::cybsp_i2c; if a second SCB is later
///          repurposed as I²C, construct a second
///          \ref sentinel::task::i2c_bus over it.
///
///          A higher-level \c byte_transport adapter
///          (\c sentinel::cyhal_i2c_bus_transport, declared in
///          \c sentinel_cyhal_i2c_bus_transport.hpp) wraps this task so
///          existing drivers (BME280, DS3231, …) can switch from direct
///          CYHAL access to bus-arbitrated access with a single type
///          substitution.
///
/// \author  galudino
/// \date    2026-05-17
/// \version 1.0 - Initial skeleton
///

#ifndef SENTINEL_TASK_I2C_BUS_HPP
#define SENTINEL_TASK_I2C_BUS_HPP

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

#include "sentinel_span.hpp"

#include <cstddef>
#include <cstdint>

namespace sentinel::task {

///
/// \brief One I²C transaction to be performed by an \ref i2c_bus.
///
/// \details The operation is implied by which spans are non-empty:
///          - \c tx non-empty, \c rx empty → write-only (STOP after write)
///          - \c tx empty, \c rx non-empty → read-only (STOP after read)
///          - \c tx non-empty, \c rx non-empty → write then read with
///            repeated start between the two phases (the typical
///            register-read pattern)
///          - both empty → no-op; the bus task replies \c success=true
///            without touching the hardware
///
///          The requester owns the lifetime of the \c tx and \c rx
///          buffers; they must remain valid until the response arrives
///          on \c response_queue. The typical pattern is a local on the
///          requester's stack plus a blocking \c xQueueReceive in the
///          same scope (see \ref i2c_bus::transact).
///
struct i2c_request {
    ///
    /// \brief 7-bit (LSB-aligned) target slave address, or 10-bit address
    ///        if the CYHAL bus has been configured for 10-bit addressing.
    ///
    uint16_t target_address;

    ///
    /// \brief Bytes to transmit before any read.
    ///
    sentinel::span<const uint8_t> tx;

    ///
    /// \brief Buffer to receive read bytes into.
    ///
    sentinel::span<uint8_t> rx;

    ///
    /// \brief Per-phase CYHAL transaction timeout, in milliseconds.
    ///
    /// \details Applied to each of the underlying
    ///          \c cyhal_i2c_master_write / \c cyhal_i2c_master_read calls.
    ///          Use \c 0 for "block until the hardware responds." For
    ///          well-behaved peripherals \c 100 ms is a reasonable
    ///          conservative default that still catches a wedged bus.
    ///
    uint32_t timeout_ms_per_phase;

    ///
    /// \brief Queue the bus task posts the \ref i2c_response onto.
    ///
    /// \details Caller-owned; the bus task does \b not create or destroy
    ///          it. May be \c nullptr if the caller is fire-and-forget,
    ///          in which case the bus task processes the request and
    ///          discards the response.
    ///
    QueueHandle_t response_queue;
};

///
/// \brief Outcome of a single \ref i2c_request, posted back to the
///        requester's response queue.
///
struct i2c_response {
    ///
    /// \brief \c true if the transaction completed with no errors after
    ///        any retries.
    ///
    bool success;

    ///
    /// \brief Raw \c cy_rslt_t from the last underlying CYHAL call.
    ///
    /// \details \c CY_RSLT_SUCCESS when \c success is \c true. On
    ///          failure, this carries the most recent error code from
    ///          \c cyhal_i2c_master_write / \c cyhal_i2c_master_read for
    ///          downstream diagnostics.
    ///
    cy_rslt_t cy_status;
};

///
/// \brief Single-owner FreeRTOS task that arbitrates access to one
///        CYHAL I²C peripheral.
///
/// \details Construct one instance per physical I²C SCB (typically as an
///          \c inline global next to the \c cyhal_i2c_t handle it owns).
///          Call \ref task_create exactly once during system
///          initialization, before any requester task starts pumping
///          requests through \ref transact.
///
/// \note    This class is non-copyable and non-movable: the task entry
///          point captures \c this by pointer, so the instance must have
///          a stable address for its lifetime.
///
class i2c_bus {
public:
    // =====================================================================
    // Defaults
    // =====================================================================

    ///
    /// \brief Default request-queue depth.
    ///
    /// \details Eight slots is enough headroom for several requester
    ///          tasks to queue up while the bus is mid-transaction.
    ///          Adjust at \ref task_create time if you have more pending
    ///          requesters than that.
    ///
    static constexpr UBaseType_t DEFAULT_QUEUE_LENGTH = 8;

    ///
    /// \brief Default FreeRTOS priority for the bus task.
    ///
    /// \details One above the standard test/driver tasks (which run at
    ///          \c configMAX_PRIORITIES - 3) so the bus task is
    ///          preferentially scheduled when a request becomes
    ///          available. Bus transactions are short; the priority bump
    ///          minimises the time between request submission and the
    ///          start of processing.
    ///
    static constexpr UBaseType_t DEFAULT_PRIORITY =
        static_cast<UBaseType_t>(configMAX_PRIORITIES - 2);

    ///
    /// \brief Default stack size (in words) for the bus task.
    ///
    /// \details \c configMINIMAL_STACK_SIZE * 4. CYHAL I²C internals plus
    ///          the request-processing loop comfortably fit; bump if you
    ///          enable verbose logging from within the bus task.
    ///
    static constexpr uint16_t DEFAULT_STACK_WORDS =
        static_cast<uint16_t>(configMINIMAL_STACK_SIZE * 4);

    ///
    /// \brief Number of immediate retries before the bus task gives up
    ///        on a single request.
    ///
    /// \details Each retry is separated by a short delay
    ///          (\ref RETRY_DELAY_MS) to let the bus recover from a
    ///          transient collision or NACK. Bosch's BMP/BME parts can
    ///          briefly NACK at the very start of init; three retries
    ///          covers that case without long-term hiding of real bus
    ///          failures.
    ///
    static constexpr uint8_t INNER_RETRIES = 3;

    ///
    /// \brief Delay between retries, in milliseconds.
    ///
    static constexpr uint32_t RETRY_DELAY_MS = 2;

    // =====================================================================
    // Construction
    // =====================================================================

    ///
    /// \brief Construct over a CYHAL I²C handle.
    ///
    /// \details The \c i2c_object must have been initialized
    ///          (\c cyhal_i2c_init_cfg or similar) and configured before
    ///          \ref task_create is called. The bus task owns the handle
    ///          for the lifetime of this instance; no other task should
    ///          call \c cyhal_i2c_master_* on the same handle.
    ///
    /// \param i2c_object Pointer to an initialised CYHAL I²C handle.
    /// \param task_name  FreeRTOS task name (for debuggers / task viewers).
    ///                   Default: \c "I2C Bus".
    ///
    explicit i2c_bus(cyhal_i2c_t *i2c_object,
                     const char *task_name = "I2C Bus") noexcept
        : m_i2c_object(i2c_object), m_task_name(task_name),
          m_request_queue(nullptr), m_up_and_running(false),
          m_task_handle(nullptr) {}

    /// Non-copyable, non-movable: the task entry-point captures \c this.
    i2c_bus(const i2c_bus &) = delete;
    i2c_bus &operator=(const i2c_bus &) = delete;
    i2c_bus(i2c_bus &&) = delete;
    i2c_bus &operator=(i2c_bus &&) = delete;

    // =====================================================================
    // Lifecycle
    // =====================================================================

    ///
    /// \brief Create the request queue and spawn the FreeRTOS task.
    ///
    /// \details Call exactly once during system initialization, before
    ///          any requester task starts. Returns the \c xTaskCreate
    ///          result so the caller can react to failure.
    ///
    /// \param priority      FreeRTOS task priority. Default:
    ///                      \ref DEFAULT_PRIORITY.
    /// \param stack_words   Task stack size in words. Default:
    ///                      \ref DEFAULT_STACK_WORDS.
    /// \param queue_length  Request-queue depth. Default:
    ///                      \ref DEFAULT_QUEUE_LENGTH.
    /// \return \c pdPASS on success, otherwise the \c xTaskCreate or
    ///         \c xQueueCreate failure code.
    ///
    BaseType_t task_create(
        UBaseType_t priority = DEFAULT_PRIORITY,
        uint16_t stack_words = DEFAULT_STACK_WORDS,
        UBaseType_t queue_length = DEFAULT_QUEUE_LENGTH) noexcept;

    ///
    /// \brief \c true once the task has reached its main loop and is
    ///        ready to accept requests.
    ///
    /// \return \c true once \ref run has started; \c false beforehand.
    ///
    bool is_running() const noexcept { return m_up_and_running; }

    // =====================================================================
    // Request submission
    // =====================================================================

    ///
    /// \brief Submit a request and block on the response.
    ///
    /// \details Convenience wrapper that sends \p request to the bus's
    ///          request queue and immediately blocks on
    ///          \c request.response_queue waiting for the
    ///          \ref i2c_response. Designed for synchronous use from a
    ///          driver-facing transport class (the typical caller).
    ///
    /// \param request                 The transaction to perform. Must
    ///                                have a non-null \c response_queue.
    /// \param request_submit_timeout  Max time to wait for a slot in the
    ///                                bus's request queue. Defaults to
    ///                                \c portMAX_DELAY.
    /// \param response_wait_timeout   Max time to wait for the response.
    ///                                Defaults to \c portMAX_DELAY.
    /// \return \c true if the bus task reported \c success; \c false on
    ///         any failure (submit timeout, response timeout, or bus
    ///         error).
    ///
    /// \note  If you need the raw \c cy_rslt_t for diagnostics, perform
    ///        a manual \c xQueueSendToBack / \c xQueueReceive pair
    ///        instead — \ref i2c_response carries it.
    ///
    bool transact(const i2c_request &request,
                  TickType_t request_submit_timeout = portMAX_DELAY,
                  TickType_t response_wait_timeout = portMAX_DELAY) noexcept;

    ///
    /// \brief Submit a request without waiting for the response.
    ///
    /// \details Fire-and-forget. The bus task still posts an
    ///          \ref i2c_response to \c request.response_queue if it is
    ///          non-null; the caller is responsible for consuming it
    ///          (or accepting that it will sit unread in the queue).
    ///          Useful for write-only "kick this command and move on"
    ///          patterns.
    ///
    /// \param request  The transaction to perform.
    /// \param timeout  Max time to wait for a slot in the request queue.
    /// \return \c pdTRUE on successful submit, \c errQUEUE_FULL on
    ///         timeout.
    ///
    BaseType_t submit(const i2c_request &request,
                      TickType_t timeout = portMAX_DELAY) noexcept;

private:
    ///
    /// \brief FreeRTOS task entry-point trampoline; recovers \c this and
    ///        calls \ref run.
    ///
    /// \param arg The \c i2c_bus instance, passed as \c this at \ref task_create.
    ///
    static void task_trampoline(void *arg);

    ///
    /// \brief Main loop. Receives requests from the queue, processes
    ///        them, posts responses. Never returns.
    ///
    [[noreturn]] void run();

    ///
    /// \brief Execute one request with internal retry logic.
    ///
    /// \details Performs the appropriate combination of
    ///          \c cyhal_i2c_master_write / \c cyhal_i2c_master_read
    ///          based on which spans in \p request are non-empty.
    ///          Retries up to \ref INNER_RETRIES times with
    ///          \ref RETRY_DELAY_MS between attempts on bus error.
    ///
    /// \param request  The transaction to perform.
    /// \return Populated \ref i2c_response.
    ///
    i2c_response process(const i2c_request &request) noexcept;

    cyhal_i2c_t      *m_i2c_object;     ///< CYHAL I²C handle (non-owning).
    const char       *m_task_name;      ///< FreeRTOS task name string.
    QueueHandle_t     m_request_queue;  ///< Inbound request queue.
    volatile bool     m_up_and_running; ///< \c true after task reaches
                                        ///< main loop.
    TaskHandle_t      m_task_handle;    ///< FreeRTOS task handle.
};

} // namespace sentinel::task

#endif /* SENTINEL_TASK_I2C_BUS_HPP */
