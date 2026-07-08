///
/// \file    sentinel_test_result.hpp
/// \brief   Shared pass/fail tally for run-to-completion test suites (#48)
///
/// \details The testbench has no host-side unit-test framework (GoogleTest,
///          Catch2, …) on the target — those need a hosted environment with
///          dynamic test registration, which a bare-metal FreeRTOS image does
///          not provide. So the testbench hand-rolls a minimal "test runner":
///          the one-shot serial orchestrator (#48) calls each suite's
///          \c run_all() in dependency order and aggregates the results.
///
///          \ref sentinel::test::tally is the stand-in for the assertion
///          accounting a framework runner would otherwise collect from
///          \c EXPECT_* / \c ASSERT_* macros. Every run-to-completion suite
///          returns one; the orchestrator sums them for a final per-group and
///          overall pass/fail summary (#48 AC \c banners_and_tally).
///
///          Header-only and dependency-free so it can be reused by #38's boot
///          orchestrator (decision #13), which shares the same one-shot
///          run-to-completion shape.
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - Initial shared test tally
///

#ifndef SENTINEL_TEST_RESULT_HPP
#define SENTINEL_TEST_RESULT_HPP

#include <cstdint>

namespace sentinel::test {

///
/// \brief Running count of passed / failed checks within a test suite.
///
/// \details A suite's \c run_all() constructs one, calls \ref record once per
///          individual test (passing that test's boolean outcome), and returns
///          it. The orchestrator accumulates suite tallies with \ref operator+=
///          to form the overall summary.
///
struct tally {
    uint16_t passed{0}; ///< Number of individual tests that passed.
    uint16_t failed{0}; ///< Number of individual tests that failed.

    ///
    /// \brief Fold one test's boolean outcome into the tally.
    ///
    /// \param ok \c true to increment \ref passed, \c false to increment
    ///           \ref failed.
    ///
    void record(bool ok) noexcept {
        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
    }

    ///
    /// \brief Total number of tests recorded.
    ///
    uint16_t total() const noexcept {
        return static_cast<uint16_t>(passed + failed);
    }

    ///
    /// \brief \c true when no test in the suite failed.
    ///
    bool all_passed() const noexcept { return failed == 0; }

    ///
    /// \brief Accumulate another tally (used by the orchestrator summary).
    ///
    tally &operator+=(const tally &other) noexcept {
        passed = static_cast<uint16_t>(passed + other.passed);
        failed = static_cast<uint16_t>(failed + other.failed);
        return *this;
    }
};

} // namespace sentinel::test

#endif /* SENTINEL_TEST_RESULT_HPP */
