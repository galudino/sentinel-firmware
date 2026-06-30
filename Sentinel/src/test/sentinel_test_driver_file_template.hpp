///
/// \file    sentinel_test_driver_file_template.hpp
/// \brief   Hardware driver test suite template (run-to-completion)
///
/// \details Copy-me scaffold for a new driver test suite. Mirrors the
///          run-to-completion convention every suite follows (#48): a single
///          synchronous \ref run_all entry that the serial test orchestrator
///          calls directly, returning a \ref sentinel::test::tally. The suite
///          does \b not self-schedule as a FreeRTOS task and does \b not own a
///          continuous read loop — continuous reading belongs in a service task
///          (\c sentinel::task::*), started by the orchestrator after the
///          one-shot suite finishes.
///
///          The bus-arbitrated transport the tests share lives in a TU-local
///          fixture in the \c .cpp (the canonical "fixture holds the shared
///          resource" shape, like a GoogleTest \c TEST_F), not a file-static
///          global.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 2.0 - Run-to-completion suite template (#48)
///

#ifndef SENTINEL_TEST_DRIVER_FILE_TEMPLATE_HPP
#define SENTINEL_TEST_DRIVER_FILE_TEMPLATE_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::driver {

///
/// \brief Run the full driver test suite to completion.
///
/// \details Constructs the fixture, runs each individual test in dependency
///          order, folds every pass/fail outcome into a
///          \ref sentinel::test::tally, and returns it. Intended to be called
///          by the testbench serial orchestrator (#48).
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::driver

#endif /* SENTINEL_TEST_DRIVER_FILE_TEMPLATE_HPP */
