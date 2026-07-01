///
/// \file    testbench.cpp
/// \brief   Testbench entry point (sentinel-testbench target)
///
/// \details Thin entry point: run the shared system bring-up
///          (\ref sentinel::resource::system_initialize), create the one-shot
///          serial test orchestrator (#48), and start the scheduler. All
///          boot-time bring-up is shared with the main-firmware target via
///          \c system_initialize; the only thing that differs between the two
///          targets is which orchestrator is created here (and the
///          \c APP_NAME_STRING banner, handled inside \c system_initialize).
///
/// \author  galudino
/// \date    2026-05-15
/// \version 2.0 - Shared system_initialize; serial test orchestrator (#48)
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
/// \brief Testbench entry point.
///
/// Runs shared system bring-up, creates the one-shot serial test orchestrator,
/// and starts the FreeRTOS scheduler (never returns in normal operation). The
/// orchestrator — running after the scheduler starts — drives every test suite
/// bottom-up and serially, then starts the continuous reader services.
///
int main(int argc, const char *argv[]) {
    sentinel::unused(argc);
    sentinel::unused(argv);

    const auto ble_stack_ok = sentinel::resource::system_initialize();

    // Create this target's one-shot orchestrator. Created before the scheduler
    // starts, but its body runs only once scheduling begins — so the bus
    // arbiters can pump the I/O its work issues (decision #13). Phase I has no
    // custom GATT DB yet (#6), so the GATT-DB-OK argument tracks the stack-init
    // result.
    auto orchestrator_result =
        sentinel::create_orchestrator(ble_stack_ok, ble_stack_ok);
    configASSERT(orchestrator_result == pdPASS);

    // Start the FreeRTOS scheduler.
    vTaskStartScheduler();

    // Should never arrive here.
    CY_ASSERT(false);
}
