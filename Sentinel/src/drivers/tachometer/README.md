# `drivers/tachometer/` — tachometer input + RPM

A small, platform-agnostic tachometer stack: an abstract pulse-input interface, a
CRTP callback helper, and a windowed RPM calculator. The concrete PSoC 6 input
lives in [`drivers/cyhal/`](../cyhal/).

| Type | File | Role |
|---|---|---|
| `tachometer_input` | [`sentinel_tachometer.hpp`](sentinel_tachometer.hpp) | Abstract pulse-input interface |
| `tach_callback_crtp<Derived>` | [`sentinel_tachometer.hpp`](sentinel_tachometer.hpp) | CRTP helper: route pulse callbacks to a `Derived` without virtual dispatch |
| `tach_rpm_windowed` | [`sentinel_tach_rpm_windowed.hpp`](sentinel_tach_rpm_windowed.hpp) | Sliding-window pulse-interval → RPM |

`tach_rpm_windowed` derives from `tach_callback_crtp<tach_rpm_windowed>`: each
edge feeds the window, and RPM is computed from the recent pulse intervals
(smoothing out jitter). Pair it with the PSoC 6 GPIO/TCPWM input in
[`drivers/cyhal/`](../cyhal/).

## See also

- [`drivers/cyhal/`](../cyhal/) — `tachometer_psoc6` (the concrete input).
- [`../README.md`](../README.md) — drivers overview · [`../../README.md`](../../README.md) — source-tree map.
