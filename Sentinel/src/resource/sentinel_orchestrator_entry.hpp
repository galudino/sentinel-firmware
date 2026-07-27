///
/// \file    sentinel_orchestrator_entry.hpp
/// \brief   Common target entry symbol so one main.cpp can serve both builds
/// (#51)
///
/// \details Each firmware target differs only in which one-shot orchestrator it
///          creates after \ref sentinel::resource::system_initialize. This
///          declares a single \ref sentinel::create_orchestrator entry symbol,
///          \b defined per-target in that target's orchestrator translation
///          unit
///          (\c sentinel_boot_orchestrator.cpp → boot orchestrator;
///          \c sentinel_test_orchestrator.cpp → test orchestrator). Because
///          the Makefile compiles exactly one of those TUs (the other dir is
///          \c CY_IGNORE'd), the linker resolves this symbol to the correct
///          orchestrator with \b no \c \#ifdef in the shared entry point.
///
///          This is the code half of #51 (unify entry points); the matching
///          config-file de-duplication + single \c main.cpp move are manual
///          ModusToolbox steps tracked in that issue.
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - Common orchestrator entry symbol
///

#ifndef SENTINEL_ORCHESTRATOR_ENTRY_HPP
#define SENTINEL_ORCHESTRATOR_ENTRY_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

namespace sentinel {

///
/// \brief Create this target's one-shot orchestrator task.
///
/// \details Defined per-target in the compiled orchestrator TU. The boot
///          orchestrator (main firmware) and the test orchestrator (testbench)
///          both take the captured BLE bring-up status so their POST probes the
///          same way.
///
/// \param ble_stack_ok \c true if the BLE stack initialized (from
///                     \ref sentinel::resource::system_initialize).
/// \param gatt_db_ok   \c true if the GATT database registered (Phase I passes
///                     the stack-init result until #6 threads the real value).
/// \return \c pdPASS on success, otherwise the \c xTaskCreate failure code.
///
BaseType_t create_orchestrator(bool ble_stack_ok, bool gatt_db_ok) noexcept;

} // namespace sentinel

#endif /* SENTINEL_ORCHESTRATOR_ENTRY_HPP */
