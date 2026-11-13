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
#include "sentinel_firmware_update_marker.hpp"
#include "sentinel_firmware_version.hpp"
#include "sentinel_system_event.hpp"

namespace {

using sentinel::firmware_version;
using sentinel::diagnostics::build_high_byte;
using sentinel::diagnostics::from_compact;
using sentinel::diagnostics::resolve_post_swap_event;
using sentinel::diagnostics::system_event;
using sentinel::diagnostics::to_compact;

///
/// \brief Log and return one named boolean outcome.
/// \param name Short case name for the serial line.
/// \param ok   The case's outcome.
/// \return \p ok.
///
bool report(const char *name, bool ok) noexcept {
    logi("  firmware_update::%-28s %s", name, ok ? "PASS" : "FAIL");
    return ok;
}

///
/// \brief \c true when two versions match in all four components.
/// \param a First version.
/// \param b Second version.
/// \return Whether \p a and \p b are the same version at full width.
///
bool same(const firmware_version &a, const firmware_version &b) noexcept {
    return a.major() == b.major() && a.minor() == b.minor() &&
           a.patch() == b.patch() && a.build() == b.build();
}

///
/// \brief Check one (from, running) pair resolves to the expected event.
/// \param name    Short case name for the serial line.
/// \param from_v  Version that started the update.
/// \param running Version now running.
/// \param want    Expected resolved event.
/// \return \c true if the resolver decided \p want.
///
bool check(const char *name, const firmware_version &from_v,
           const firmware_version &running, system_event want) noexcept {
    return report(name, resolve_post_swap_event(from_v, running) == want);
}

///
/// \brief A version survives compact packing + its build high byte (#86).
/// \param name Short case name for the serial line.
/// \param v    Version to round-trip.
/// \return \c true if every component comes back unchanged.
///
bool round_trips(const char *name, const firmware_version &v) noexcept {
    return report(name, same(from_compact(to_compact(v), build_high_byte(v)), v));
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

    // #86: builds share a low byte (111 = 0x06F, 367 = 0x16F) but are
    // different images — compared at full width this is completed, not the
    // false "reverted" the old low-byte comparison produced.
    t.record(check("completed_on_build_above_255",
                   firmware_version(1, 0, 2, 111),
                   firmware_version(1, 0, 2, 367),
                   se::firmware_update_completed));
    t.record(check("reverted_on_same_high_build",
                   firmware_version(1, 0, 2, 367),
                   firmware_version(1, 0, 2, 367), se::firmware_reverted));

    // #86: compact + build high byte reassembles every version, up to the max.
    t.record(round_trips("round_trip_max_version",
                         firmware_version(255, 255, 255, 65535)));
    t.record(round_trips("round_trip_build_367", firmware_version(1, 0, 2, 367)));
    // A pre-#86 record has build_hi == 0 → reassembles to its old value.
    t.record(report("pre86_record_decodes_unchanged",
                    same(from_compact(to_compact(firmware_version(1, 0, 2, 111)),
                                      0u),
                         firmware_version(1, 0, 2, 111))));

    // #86: the pending-update marker keeps the full build; a marker written
    // before #86 (from_build_hi zeroed) reads back its old low-byte build.
    auto marker = sentinel::storage::firmware_update_marker{};
    marker.from_build = 0x6Fu;
    marker.from_build_hi = 0x01u;
    t.record(report("marker_full_build", marker.from_build_full() == 367u));
    marker.from_build_hi = 0u;
    t.record(report("marker_pre86_build", marker.from_build_full() == 111u));

    return t;
}
