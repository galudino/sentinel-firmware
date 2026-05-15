#ifndef SENTINEL_TIMER_HPP
#define SENTINEL_TIMER_HPP

#include <cstdint>

namespace sentinel {

template <typename Implementation>
class timer {
public:
    uint32_t set_frequency(uint32_t frequency_hz) noexcept {
        return impl().set_frequency(frequency_hz);
    }

    uint32_t start() noexcept { return impl().start(); }
    uint32_t stop() noexcept { return impl().stop(); }
    uint32_t reset() noexcept { return impl().reset(); }
    uint32_t read() noexcept { return impl().read(); }

private:
    Implementation &impl() noexcept {
        return static_cast<Implementation &>(*this);
    }

    const Implementation &impl() const noexcept {
        return static_cast<const Implementation &>(*this);
    }
};

} // namespace sentinel

#endif /* SENTINEL_TIMER_HPP */
