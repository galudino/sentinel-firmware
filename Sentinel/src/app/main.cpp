///
/// \file    main.cpp
/// \brief   Main firmware entry point (sentinel-firmware target)
///
/// \details Thin entry point: run the shared system bring-up
///          (\ref sentinel::resource::system_initialize), create the one-shot
///          boot orchestrator (#38), and start the scheduler. All boot-time
///          bring-up is shared with the testbench target via
///          \c system_initialize; the only thing that differs between the two
///          targets is which orchestrator is created here.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 2.0 - Shared system_initialize; boot orchestrator (#38)
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

///< Boot orchestrator — one-shot boot sequence + service-task spawner (#38).
#include "sentinel_app_orchestrator.hpp"

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

    // Create the one-shot boot orchestrator. Created here before the scheduler
    // starts, but its body runs only once scheduling begins — so the bus
    // arbiters can pump the I/O its POST probes + device-context construction
    // issue (decision #13). Phase I has no custom GATT DB yet (#6), so the
    // GATT-DB-OK argument tracks the stack-init result.
    auto orchestrator_result =
        sentinel::app::boot_orchestrator::instance().task_create(ble_stack_ok,
                                                                 ble_stack_ok);
    configASSERT(orchestrator_result == pdPASS);

    // Start the FreeRTOS scheduler.
    vTaskStartScheduler();

    // Should never arrive here.
    CY_ASSERT(false);
}
