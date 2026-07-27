# `drivers/led/` — PWM-driven LED

A thin driver that treats an LED as a brightness output over a **PWM signal**. It
is parameterised over a [`pwm_signal`](../../transport/platform_agnostic/)
implementation, so it is platform-agnostic like the rest of `drivers/`.

| | |
|---|---|
| **Key type** | `sentinel::led_pwm<PWMImplementation>` |
| **File** | [`sentinel_led_pwm.hpp`](sentinel_led_pwm.hpp) (header-only) |

`led_pwm` derives from `pwm_signal<PWMImplementation>`, adding LED-oriented
semantics on top of the generic PWM façade: a `duty_cycle` enum
(`off` / `blinking` / `on`) and `set_blink_rate()`.

```cpp
sentinel::led_pwm<sentinel::cyhal_pwm_signal> led(pwm);
led.start();
led.set_blink_rate(decltype(led)::duty_cycle::blinking);   // 50% duty
```

## See also

- [`transport/platform_agnostic/`](../../transport/platform_agnostic/) — `pwm_signal` base · [`transport/cyhal/`](../../transport/cyhal/) — `cyhal_pwm_signal`.
- [`../README.md`](../README.md) — drivers overview · [`../../README.md`](../../README.md) — source-tree map.
