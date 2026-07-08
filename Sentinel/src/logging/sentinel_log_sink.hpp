///
/// \file       sentinel_log_sink.hpp
/// \brief      Log severity level + portable log-sink backends - Header
///
/// \details    Defines the \ref sentinel::logging::level severity enum and the
///             thin, platform-selectable **sink** backends the logging facade
///             (\ref sentinel_log.hpp) fans a message out to. Each sink exposes
///             a single \c write entry point that consumes an already-formatted
///             line, so the facade formats **once** and every sink emits the
///             identical bytes.
///
///             Backends (selected at compile time, mirroring the CRTP transport
///             pattern used for buses):
///               - \ref sink::serial   — cyhal/Infineon retarget-io \c printf
///               - \ref sink::ble_debug — BLE debug output stream ring buffer
///
///             Future platforms add a sink here (POSIX \c std::cout, Nordic
///             RTT/SEGGER) without touching call sites.
///
/// \author     galudino
/// \date       2026-07-08
///

#ifndef SENTINEL_LOG_SINK_HPP
#define SENTINEL_LOG_SINK_HPP

#include <cstddef>
#include <cstdint>

namespace sentinel::logging {

///
/// \brief      Log severity level, ordered least-to-most severe.
///
enum class level : uint8_t {
    debug, ///< Verbose diagnostic detail; suppressed on the serial sink.
    info,  ///< Normal informational progress.
    warn,  ///< Recoverable / degraded condition.
    error  ///< Failure.
};

///
/// \brief      Map a \ref level to its lowercase prefix string.
///
/// \param      l   Severity level.
/// \return     One of "debug", "info", "warn", "error".
///
const char *to_string(level l) noexcept;

namespace sink {

///
/// \brief      Serial backend (cyhal/Infineon): writes a fully-formatted line
///             to the retarget-io UART via \c printf.
///
/// \details    Level-gated to \ref level::info and above, matching the prior
///             \c cy_log(CY_LOG_INFO) serial behaviour (\c debug lines stay off
///             the wire).
///
struct serial {
    ///
    /// \brief      Emit a pre-formatted line on the serial UART.
    ///
    /// \param      l       Severity of the line (gate: >= info).
    /// \param      line    Null-terminated, fully-formatted log line.
    /// \param      len     Length of \p line excluding the null terminator.
    ///
    static void write(level l, const char *line, size_t len) noexcept;
};

///
/// \brief      BLE debug-stream backend (#25): pushes a fully-formatted line
///             into the debug ring buffer for notification to the client.
///
/// \details    All-or-nothing: if the whole line will not fit in the ring, it
///             is dropped rather than partially written (a partial line renders
///             as a corrupt entry on the client).
///
struct ble_debug {
    ///
    /// \brief      Enqueue a pre-formatted line for the BLE debug stream.
    ///
    /// \param      l       Severity of the line (currently unfiltered).
    /// \param      line    Null-terminated, fully-formatted log line.
    /// \param      len     Length of \p line excluding the null terminator.
    ///
    static void write(level l, const char *line, size_t len) noexcept;
};

} // namespace sink

} // namespace sentinel::logging

#endif /* SENTINEL_LOG_SINK_HPP */
