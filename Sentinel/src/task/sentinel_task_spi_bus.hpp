///
/// \file    sentinel_task_spi_bus.hpp
/// \brief   FreeRTOS SPI bus arbiter task (request/response queue model)
///
/// \details This header declares \ref sentinel::task::spi_bus, a dedicated
///          FreeRTOS task that serves as the single owner of one CYHAL SPI
///          peripheral handle (\c cyhal_spi_t*). Every other task in the
///          system that needs to transact on that bus does so by submitting
///          a \ref sentinel::task::spi_request to the bus's request queue and
///          blocking on its own response queue. The bus task processes one
///          request at a time, switches the SCB's active slave-select line to
///          the one named in the request, performs the transfer, and posts an
///          \ref sentinel::task::spi_response back to the requester.
///
///          Direct counterpart of \ref sentinel::task::i2c_bus — same
///          ownership model, same retry behaviour, same priority. The
///          differences:
///          - Each request carries a \c cyhal_gpio_t identifying the
///            slave-select pin to assert for this transaction. The SCB
///            hardware handles the actual CS toggling around the transfer
///            once \c cyhal_spi_select_active_ssel has routed that SS line
///            as the active one; the arbiter never drives CS as a plain
///            GPIO.
///          - SPI is full-duplex on the wire. The same
///            \ref sentinel::task::spi_request carries both \c tx and \c rx
///            spans; whichever is non-empty
///            (or both) drives \c cyhal_spi_transfer with the appropriate
///            \c write_fill byte clocked out on MOSI when only reading.
///
///          Limit of four slaves per bus (SS0..SS3) is a CYHAL/SCB
///          constraint, not an arbiter constraint. Phase 1 + 2 mix
///          (W25Q128, BME280-on-SPI, possible display, possible motor)
///          fits comfortably inside that.
///
/// \author  galudino
/// \date    2026-05-18
/// \version 1.0 - Initial skeleton
///

#ifndef SENTINEL_TASK_SPI_BUS_HPP
#define SENTINEL_TASK_SPI_BUS_HPP

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

#include "sentinel_span.hpp"

#include <cstddef>
#include <cstdint>

namespace sentinel::task {

///
/// \brief One SPI transaction to be performed by an \ref spi_bus.
///
/// \details The transaction is one CS-asserted window:
///          - \c tx non-empty, \c rx empty → write-only.
///          - \c tx empty, \c rx non-empty → read-only; the arbiter
///            clocks \c write_fill on MOSI for each received byte.
///          - both non-empty → \c cyhal_spi_transfer runs for
///            \c max(tx.size(), rx.size()) bytes total: \c tx provides
///            the MOSI bytes (padded with \c write_fill if shorter than
///            \c rx), \c rx captures the MISO bytes (extra bytes
///            discarded if \c rx is shorter than the total length).
///            This is the natural shape for "send a command, then clock
///            in a response" flows — the caller usually sizes \c rx to
///            \c tx.size() + response_length and ignores the leading
///            bytes that correspond to the command echo.
///          - both empty → no-op; the bus task replies \c success=true
///            without touching the hardware.
///
///          The caller owns the lifetime of the \c tx and \c rx buffers;
///          they must remain valid until the response arrives on
///          \c response_queue. The typical pattern is a stack-local plus
///          a blocking \c xQueueReceive in the same scope.
///
struct spi_request {
    ///
    /// \brief GPIO pin acting as the slave-select line for this device.
    ///
    /// \details The arbiter calls \c cyhal_spi_select_active_ssel with
    ///          this pin before the transfer so the SCB asserts it
    ///          automatically around the transaction. Must have been
    ///          assigned as one of SS0..SS3 in Device Configurator.
    ///
    cyhal_gpio_t ssel;

    ///
    /// \brief Bytes to transmit on MOSI.
    ///
    sentinel::span<const uint8_t> tx;

    ///
    /// \brief Buffer to capture bytes received on MISO.
    ///
    sentinel::span<uint8_t> rx;

    ///
    /// \brief Byte clocked on MOSI when only reading or when \c rx is
    ///        longer than \c tx.
    ///
    /// \details \c 0xFF is the conventional choice for SPI NOR flash and
    ///          most sensors (the bus idles high). \c 0x00 is appropriate
    ///          for some controller ICs that expect explicit-zero padding.
    ///
    uint8_t write_fill;

    ///
    /// \brief Queue the bus task posts the \ref spi_response onto.
    ///
    /// \details Caller-owned; the bus task does \b not create or destroy
    ///          it. May be \c nullptr if the caller is fire-and-forget,
    ///          in which case the bus task processes the request and
    ///          discards the response.
    ///
    QueueHandle_t response_queue;
};

///
/// \brief Outcome of a single \ref spi_request, posted back to the
///        requester's response queue.
///
struct spi_response {
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
    ///          \c cyhal_spi_select_active_ssel or \c cyhal_spi_transfer
    ///          for downstream diagnostics.
    ///
    cy_rslt_t cy_status;
};

///
/// \brief Single-owner FreeRTOS task that arbitrates access to one
///        CYHAL SPI peripheral.
///
/// \details Construct one instance per physical SPI SCB (typically as an
///          \c inline global next to the \c cyhal_spi_t handle it owns).
///          Call \ref task_create exactly once during system
///          initialization, before any requester task starts pumping
///          requests through \ref transact.
///
/// \note    This class is non-copyable and non-movable: the task entry
///          point captures \c this by pointer, so the instance must have
///          a stable address for its lifetime.
///
class spi_bus {
public:
    // =====================================================================
    // Defaults
    // =====================================================================

    /// \brief Default request-queue depth.
    static constexpr UBaseType_t DEFAULT_QUEUE_LENGTH = 8;

    /// \brief Default FreeRTOS priority for the bus task.
    static constexpr UBaseType_t DEFAULT_PRIORITY =
        static_cast<UBaseType_t>(configMAX_PRIORITIES - 2);

    /// \brief Default stack size (in words) for the bus task.
    static constexpr uint16_t DEFAULT_STACK_WORDS =
        static_cast<uint16_t>(configMINIMAL_STACK_SIZE * 4);

    /// \brief Number of immediate retries before the bus task gives up on a
    ///        single request.
    static constexpr uint8_t INNER_RETRIES = 3;

    /// \brief Delay between retries, in milliseconds.
    static constexpr uint32_t RETRY_DELAY_MS = 2;

    ///
    /// \brief Default fill byte clocked on MOSI when reading without a
    ///        caller-supplied transmit payload.
    ///
    static constexpr uint8_t DEFAULT_WRITE_FILL = 0xFF;

    // =====================================================================
    // Construction
    // =====================================================================

    ///
    /// \brief Construct over a CYHAL SPI handle.
    ///
    /// \param spi_object Pointer to an initialised CYHAL SPI handle.
    /// \param task_name  FreeRTOS task name. Default: \c "SPI Bus".
    ///
    explicit spi_bus(cyhal_spi_t *spi_object,
                     const char *task_name = "SPI Bus") noexcept
        : m_spi_object(spi_object), m_task_name(task_name),
          m_request_queue(nullptr), m_up_and_running(false),
          m_task_handle(nullptr) {}

    /// Non-copyable, non-movable: the task entry-point captures \c this.
    spi_bus(const spi_bus &) = delete;
    spi_bus &operator=(const spi_bus &) = delete;
    spi_bus(spi_bus &&)                 = delete;
    spi_bus &operator=(spi_bus &&)      = delete;

    // =====================================================================
    // Lifecycle
    // =====================================================================

    ///
    /// \brief Create the request queue and spawn the FreeRTOS task.
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
        UBaseType_t priority      = DEFAULT_PRIORITY,
        uint16_t    stack_words   = DEFAULT_STACK_WORDS,
        UBaseType_t queue_length  = DEFAULT_QUEUE_LENGTH) noexcept;

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
    /// \param request                 The transaction to perform. Must
    ///                                have a non-null \c response_queue.
    /// \param request_submit_timeout  Max time to wait for a slot in the
    ///                                bus's request queue.
    /// \param response_wait_timeout   Max time to wait for the response.
    /// \return \c true if the bus task reported \c success; \c false on
    ///         any failure (submit timeout, response timeout, or bus error).
    ///
    bool transact(const spi_request &request,
                  TickType_t request_submit_timeout = portMAX_DELAY,
                  TickType_t response_wait_timeout  = portMAX_DELAY) noexcept;

    ///
    /// \brief Submit a request without waiting for the response.
    ///
    /// \param request The transaction to perform.
    /// \param timeout Max time to wait for a slot in the request queue.
    /// \return \c pdTRUE on successful submit, \c errQUEUE_FULL on timeout.
    ///
    BaseType_t submit(const spi_request &request,
                      TickType_t timeout = portMAX_DELAY) noexcept;

private:
    /// \brief FreeRTOS task entry-point trampoline; recovers \c this and
    ///        calls \ref run.
    /// \param arg The \c spi_bus instance, passed as \c this at \ref task_create.
    static void task_trampoline(void *arg);

    /// \brief Main loop. Receives requests from the queue, processes them,
    ///        posts responses. Never returns.
    [[noreturn]] void run();

    ///
    /// \brief Execute one request with internal retry logic.
    ///
    /// \param request The transaction to perform.
    /// \return Populated \ref spi_response.
    ///
    spi_response process(const spi_request &request) noexcept;

    cyhal_spi_t      *m_spi_object;     ///< CYHAL SPI handle (non-owning).
    const char       *m_task_name;      ///< FreeRTOS task name string.
    QueueHandle_t     m_request_queue;  ///< Inbound request queue.
    volatile bool     m_up_and_running; ///< \c true after task reaches main loop.
    TaskHandle_t      m_task_handle;    ///< FreeRTOS task handle.
};

} // namespace sentinel::task

#endif /* SENTINEL_TASK_SPI_BUS_HPP */
