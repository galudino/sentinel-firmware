///
/// \file    main.cpp
/// \brief   Shared entry point for both firmware targets (#51)
///
/// \details One entry point serves the main-firmware and testbench builds: run
///          the shared system bring-up (\ref sentinel::resource::system_initialize),
///          create this target's one-shot orchestrator via the common
///          \ref sentinel::create_orchestrator symbol, and start the scheduler.
///          The symbol is defined per-target in that target's orchestrator TU
///          (\c src/app → boot orchestrator #38; \c src/testbench → test
///          orchestrator #48); the Makefile \c CY_IGNORE's the other dir, so the
///          linker resolves exactly one definition with \b no \c #ifdef here.
///          See \ref sentinel_orchestrator_entry.hpp.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 3.0 - Unified app/testbench entry point (#51)
///

// Always wrap C includes in diagnostic push/pop, in an extern "C" block, to
// avoid pedantic warnings.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cybsp.h" ///< CY_ASSERT
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

///< Per-target orchestrator entry symbol (#51) — resolves to this target's
///< orchestrator at link time, so this entry point is target-agnostic.
#include "sentinel_orchestrator_entry.hpp"

///< Shared system bring-up + Device Configurator resources.
#include "sentinel_resource.hpp"

///< Utilities
#include "sentinel_utilities.hpp"

///
/// \brief Application entry point.
///
/// Runs shared system bring-up, creates the one-shot boot orchestrator, and
/// starts the FreeRTOS scheduler (never returns in normal operation).
///
int main(int argc, const char *argv[]) {
    sentinel::unused(argc);
    sentinel::unused(argv);

    const auto ble_stack_ok = sentinel::resource::system_initialize();

    // Create this target's one-shot orchestrator. Created before the scheduler
    // starts, but its body runs only once scheduling begins — so the bus
    // arbiters can pump the I/O its work issues (decision #13). The GATT DB (#6)
    // registers asynchronously in BTM_ENABLED; the orchestrator reads that real
    // result live at POST, so the argument here is only a pre-scheduler fallback.
    auto orchestrator_result =
        sentinel::create_orchestrator(ble_stack_ok, ble_stack_ok);
    configASSERT(orchestrator_result == pdPASS);

    // Start the FreeRTOS scheduler.
    vTaskStartScheduler();

    // Should never arrive here.
    CY_ASSERT(false);
}
