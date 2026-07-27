# `drivers/cyhal/` — PSoC 6-specific driver bindings

Concrete, **PSoC 6-specific** driver pieces that bind an abstract driver interface
to CYHAL peripherals. Where a driver in the sibling folders is pure logic
parameterised over a [transport](../../transport/), the platform-specific input it
needs lands here.

| Type | File | Binds |
|---|---|---|
| `tachometer_psoc6` | [`sentinel_tachometer_psoc6.hpp`](sentinel_tachometer_psoc6.hpp) | [`tachometer_input`](../tachometer/) → PSoC 6 GPIO + TCPWM capture |

`tachometer_psoc6` drives the [`tachometer`](../tachometer/) stack from real
hardware: a GPIO edge / TCPWM counter produces the pulse events that
`tach_rpm_windowed` turns into RPM.

> Most drivers keep their platform binding *inside* the transport template
> argument, so they need no entry here. This folder is for the cases (like the
> tachometer's edge capture) where the input is inherently PSoC 6-specific.

## See also

- [`drivers/tachometer/`](../tachometer/) — the abstract interface + RPM windowing.
- [`transport/cyhal/`](../../transport/cyhal/) — the CYHAL transports proper.
- [`../README.md`](../README.md) — drivers overview · [`../../README.md`](../../README.md) — source-tree map.
