///
/// \file    sentinel_test_firmware_update.cpp
/// \brief   Boot-time swap-confirm resolver tests (#81)
///
/// \details Implements the tests declared in \ref
///          sentinel_test_firmware_update.hpp. Each case calls the pure resolver
///          \ref sentinel::diagnostics::resolve_post_swap_event with a
///          (from, running) version pair and checks the decided event. No flash
///          or RAM store is needed — this is the deterministic logic the #81
///          boot detector depends on, isolated from its I/O.
///
/// \author  galudino
/// \date    2026-11-13
/// \version 1.0 - Swap-confirm resolver tests (#81)
///

#include "sentinel_test_firmware_update.hpp"

#include "sentinel_debug_print.hpp"
#include "sentinel_firmware_version.hpp"
#include "sentinel_system_event.hpp"

namespace {

using sentinel::firmware_version;
using sentinel::diagnostics::resolve_post_swap_event;
using sentinel::diagnostics::system_event;
using sentinel::diagnostics::to_compact;

///
/// \brief Check one (from, running) pair resolves to the expected event.
/// \param name    Short case name for the serial line.
/// \param from_v  Version that started the update (packed to compact here).
/// \param running Version now running.
/// \param want    Expected resolved event.
/// \return \c true if the resolver decided \p want.
///
bool check(const char *name, const firmware_version &from_v,
           const firmware_version &running, system_event want) noexcept {
    const bool ok = resolve_post_swap_event(to_compact(from_v), running) == want;
    logi("  firmware_update::%-24s %s", name, ok ? "PASS" : "FAIL");
    return ok;
}

} // namespace

// ============================================================================
// sentinel::test::firmware_update::run_all
// ============================================================================

sentinel::test::tally sentinel::test::firmware_update::run_all() noexcept {
    auto t = sentinel::test::tally{};
    using se = sentinel::diagnostics::system_event;

    // A different image is running → the swap took effect → completed, in every
    // direction: the real WIP A/B images are 1.0.2.107 <-> 51.0.2.107 (#51).
    t.record(check("completed_on_upgrade", firmware_version(1, 0, 2, 107),
                   firmware_version(51, 0, 2, 107),
                   se::firmware_update_completed));
    t.record(check("completed_on_downgrade", firmware_version(51, 0, 2, 107),
                   firmware_version(1, 0, 2, 107),
                   se::firmware_update_completed));
    t.record(check("completed_on_minor_change", firmware_version(1, 0, 2, 107),
                   firmware_version(1, 1, 2, 107),
                   se::firmware_update_completed));
    t.record(check("completed_on_build_only", firmware_version(1, 0, 2, 107),
                   firmware_version(1, 0, 2, 108),
                   se::firmware_update_completed));

    // The same image is still running → no swap took effect → reverted.
    t.record(check("reverted_on_same_version", firmware_version(1, 0, 2, 107),
                   firmware_version(1, 0, 2, 107), se::firmware_reverted));

    return t;
}
