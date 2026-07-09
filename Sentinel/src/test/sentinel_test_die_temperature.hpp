///
/// \file    sentinel_test_die_temperature.hpp
/// \brief   On-target test suite for the PSoC 6 die-temperature driver (#55)
///
/// \details Run-to-completion suite (#48 shape) exercising
///          \ref sentinel::drivers::psoc6_die_temperature against the \b real
///          SAR ADC on the CYBLE-416045 (decision #15 — the testbench tests real
///          components on hardware). Validates bring-up, that a reading is
///          produced, that it lands in a plausible physical range, and that
///          successive readings are stable. Cross-sensor comparison (die vs
///          BME280 ambient vs DS3231) is exercised by the application's
///          \c cpu_die_temp_service, where those sensors sample continuously.
///
/// \author  galudino
/// \date    2026-07-09
/// \version 1.0
///

#ifndef SENTINEL_TEST_DIE_TEMPERATURE_HPP
#define SENTINEL_TEST_DIE_TEMPERATURE_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::die_temperature {

///
/// \brief Run the whole die-temperature suite and return its pass/fail tally.
///
/// \return The suite's pass/fail \ref sentinel::test::tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::die_temperature

#endif /* SENTINEL_TEST_DIE_TEMPERATURE_HPP */
