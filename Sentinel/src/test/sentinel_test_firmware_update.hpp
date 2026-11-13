///
/// \file    sentinel_test_firmware_update.hpp
/// \brief   Boot-time swap-confirm resolver test declarations (#81)
///
/// \details Declares the testbench-side tests for the #81 post-swap event
///          resolver (\ref sentinel::diagnostics::resolve_post_swap_event). The
///          resolver is the pure decision at the heart of the boot-time
///          swap-confirm detector: given the version that started an OTA (from
///          a durable marker that survived the reset) and the version now
///          running, it decides whether the MCUBoot swap took effect
///          (\c firmware_update_completed) or not (\c firmware_reverted). Being
///          pure, it is exercised directly here — off-bench, no flash —
///          covering upgrade, downgrade, build-only and minor-only changes (all
///          completed) and the same-version case (reverted).
///
/// \author  galudino
/// \date    2026-11-13
/// \version 1.0 - Swap-confirm resolver test declarations (#81)
///

#ifndef SENTINEL_TEST_FIRMWARE_UPDATE_HPP
#define SENTINEL_TEST_FIRMWARE_UPDATE_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::firmware_update {

///
/// \brief Run the swap-confirm resolver test suite to completion.
///
/// \details Drives \ref sentinel::diagnostics::resolve_post_swap_event across
///          its decision space and returns the pass/fail
///          \ref sentinel::test::tally. Run-to-completion (#48): the serial
///          test orchestrator calls this directly.
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::firmware_update

#endif /* SENTINEL_TEST_FIRMWARE_UPDATE_HPP */
