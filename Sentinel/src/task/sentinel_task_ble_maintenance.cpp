///
/// \file    sentinel_task_ble_maintenance.cpp
/// \brief   Async BLE-triggered maintenance task implementation (#6)
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cyabs_rtos.h"
#include "cyhal.h"

#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include "sentinel_debug_print.hpp"
#include "sentinel_device_context.hpp"
#include "sentinel_gatt_paged.hpp"
#include "sentinel_task_ble_maintenance.hpp"

#include <cstdint>

namespace sentinel::task {

namespace {

constexpr uint32_t BIT_CLEAR_SNAPSHOTS =
    1u << 0; ///< Request: erase Snapshot History.
constexpr uint32_t BIT_CLEAR_EVENTS =
    1u << 1; ///< Request: erase System Event Log.
constexpr uint32_t BIT_BOOTLOADER = 1u << 2; ///< Request: enter the bootloader.

/// \brief Grace period before the bootloader reset so the write response +
///        disconnect can flush.
constexpr uint32_t BOOTLOADER_RESET_DELAY_MS = 1000;

} // namespace

ble_maintenance_task &ble_maintenance_task::instance() noexcept {
    static ble_maintenance_task the_instance;
    return the_instance;
}

BaseType_t ble_maintenance_task::task_create(UBaseType_t priority,
                                             uint16_t stack_words) noexcept {
    return xTaskCreate(&ble_maintenance_task::task_trampoline,
                       "BLE Maintenance", stack_words, this, priority,
                       &m_handle);
}

void ble_maintenance_task::task_trampoline(void *task_parameter) {
    static_cast<ble_maintenance_task *>(task_parameter)->run();
}

void ble_maintenance_task::notify(uint32_t bit) noexcept {
    if (m_handle != nullptr) {
        xTaskNotify(m_handle, bit, eNotifyAction::eSetBits);
    }
}

void ble_maintenance_task::request_clear_snapshots() noexcept {
    notify(BIT_CLEAR_SNAPSHOTS);
}

void ble_maintenance_task::request_clear_events() noexcept {
    notify(BIT_CLEAR_EVENTS);
}

void ble_maintenance_task::request_bootloader() noexcept {
    notify(BIT_BOOTLOADER);
}

void ble_maintenance_task::run() {
    while (true) {
        uint32_t bits = 0;
        // Block until a request arrives; clear all bits on exit.
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);

        if ((bits & BIT_CLEAR_SNAPSHOTS) && resource::context_ready()) {
            logi("ble_maintenance: clearing snapshot history...");
            if (resource::context().snapshot_store.erase_all()) {
                sentinel::gatt::paged::refresh_snapshot_count();
                logi("ble_maintenance: snapshot history cleared");
            } else {
                loge("ble_maintenance: snapshot history erase failed");
            }
        }

        if ((bits & BIT_CLEAR_EVENTS) && resource::context_ready()) {
            logi("ble_maintenance: clearing event log...");
            if (resource::context().event_store.erase_all()) {
                sentinel::gatt::paged::refresh_event_count();
                logi("ble_maintenance: event log cleared");
            } else {
                loge("ble_maintenance: event log erase failed");
            }
        }

        if (bits & BIT_BOOTLOADER) {
            logi("ble_maintenance: bootloader requested; resetting in %u ms",
                 static_cast<unsigned>(BOOTLOADER_RESET_DELAY_MS));
            cy_rtos_delay_milliseconds(BOOTLOADER_RESET_DELAY_MS);
            NVIC_SystemReset();
        }
    }
}

} // namespace sentinel::task
