///
/// \file    sentinel_tachometer.hpp
/// \brief   Abstract tachometer input interface and CRTP callback helper
///
/// \details Provides a platform-agnostic abstraction for receiving tachometer
///          pulse edge events. This module defines:
///          - tachometer_input: Abstract base class for edge event receivers
///          - tach_callback_crtp: CRTP helper to avoid virtual call overhead
///
///          The tachometer interface is designed to be called from ISR context
///          when a pulse edge is detected. The timestamp provided represents
///          a monotonic counter value (e.g., TCPWM ticks, SysTick, etc.).
///
///          Semantics:
///          - on_edge(tick): Called from ISR context on each detected edge
///          - handle_edge(tick): CRTP compile-time dispatch target
///
/// \example
/// \code
/// // Using CRTP for zero-overhead edge handling
/// class my_tach_handler : public sentinel::tach_callback_crtp<my_tach_handler> {
/// public:
///     void handle_edge(uint32_t tick) {
///         // Process the edge event with timestamp
///         last_tick_ = tick;
///     }
/// private:
///     uint32_t last_tick_{0};
/// };
///
/// // Connect to a platform-specific tachometer source
/// my_tach_handler handler;
/// sentinel::tachometer_psoc6 tach_source(handler, BLOWER_TACH_PIN, &counter);
/// \endcode
///
/// \date    2026-05-15
/// \version 1.0 - Initial tachometer interface
///

#ifndef SENTINEL_TACHOMETER_HPP
#define SENTINEL_TACHOMETER_HPP

#include <cstdint>

namespace sentinel {

class tachometer_input;

template <typename Derived>
class tach_callback_crtp;

} // namespace sentinel

///
/// \brief Abstract interface for tachometer edge event receivers
///
/// \details This class defines the contract for any component that wants to
///          receive tachometer pulse edge events. The timestamp tick is
///          expressed in units of some monotonic counter (e.g., TCPWM ticks,
///          SysTick, etc.).
///
///          Implementations should be prepared to handle calls from ISR
///          context and should keep processing minimal to avoid blocking
///          other interrupts.
///
class sentinel::tachometer_input {
public:
    ///
    /// \brief Virtual destructor for proper cleanup
    ///
    virtual ~tachometer_input() = default;

    ///
    /// \brief Called from ISR context on each tachometer edge
    ///
    /// \details This method is invoked by the hardware abstraction layer
    ///          when a tachometer pulse edge is detected. Implementations
    ///          should keep processing minimal as this runs in ISR context.
    ///
    /// \param tick Monotonic timestamp in counter ticks
    ///
    virtual void on_edge(uint32_t tick) = 0;
};

///
/// \brief CRTP helper for compile-time dispatch of tachometer events
///
/// \details This class template provides a bridge between the virtual
///          on_edge() interface and a compile-time on_edge() method
///          in the derived type. Using CRTP eliminates virtual call overhead
///          while maintaining the abstract interface for dependency injection.
///
/// \tparam Derived The derived class that implements handle_edge(uint32_t)
///
/// \example
/// \code
/// class my_tach : public sentinel::tach_callback_crtp<my_tach> {
/// public:
///     void on_edge(uint32_t tick) {
///         // Process edge event
///     }
/// };
/// \endcode
///
template <typename Derived>
class sentinel::tach_callback_crtp : public tachometer_input {
public:
    ///
    /// \brief Dispatch edge event to derived class
    ///
    /// \details Routes the virtual on_edge() call to the derived class's
    ///          on_edge() method using static polymorphism.
    ///
    /// \param tick Monotonic timestamp in counter ticks
    ///
    void on_edge(uint32_t tick) override {
        static_cast<Derived *>(this)->on_edge(tick);
    }

    uint32_t pulses_in_last_window() const noexcept {
        return static_cast<Derived *>(this)->pulses_in_last_window();
    }
};

#endif /* SENTINEL_TACHOMETER_HPP */
