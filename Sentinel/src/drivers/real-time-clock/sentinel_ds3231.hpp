///
/// \file    sentinel_ds3231.hpp
/// \brief   DS3231 real-time clock driver (I²C)
///
/// \details This header provides a complete, datasheet-derived driver for the
///          Maxim/Analog Devices DS3231 extremely-accurate I²C real-time
///          clock with an integrated temperature-compensated crystal
///          oscillator (TCXO) and crystal. The driver is implemented purely
///          in C++; there is no underlying vendor C library being wrapped.
///
///          Every register documented in the DS3231 datasheet
///          (registers \c 0x00 – \c 0x12) has a named entry in
///          \ref register_address, and every named feature in the datasheet
///          has a corresponding typed accessor on the class. Bit positions
///          for the Control (\c 0x0E) and Status (\c 0x0F) registers are
///          collected in \ref control_register and \ref status_register
///          nested types so callers never refer to magic numbers.
///
///          Public API design (matches \ref sentinel::bme280):
///          - Operations that produce a value return \c std::optional<T>.
///          - Operations that just act on the device return \c bool
///            (\c true on success).
///          - The most recent low-level error from the bus or from input
///            validation is exposed via \ref last_error(), typed as
///            \ref err. The "value-or-nullopt" idiom keeps the happy path
///            compact; the forensic accessor keeps granular error
///            information recoverable from a debugger or log line.
///
///          Time/date representation:
///          - The DS3231 stores time and date in BCD (binary-coded decimal)
///            and supports both 12-hour and 24-hour modes. This driver
///            unconditionally programmes 24-hour mode, both on time-set and
///            for alarms, so callers always deal in 0–23 hours and never
///            have to think about the AM/PM bit.
///          - The day-of-week field is a single byte that the DS3231 leaves
///            entirely up to the user. This driver adopts the ISO 8601
///            convention encoded in \ref day_of_week:
///            \c 1 = Monday, …, \c 7 = Sunday.
///          - The century bit in register \c 0x05 distinguishes the
///            21st century (\c 0 → years 2000–2099) from the 22nd
///            (\c 1 → years 2100–2199). The DS3231 has no representation
///            for years prior to 2000.
///
///          Temperature:
///          - The on-die temperature sensor (\c 0x11 / \c 0x12) updates
///            every 64 s during normal operation. A user-forced conversion
///            can be triggered via \ref start_temperature_conversion;
///            \ref is_temperature_conversion_busy polls the BSY flag while
///            the conversion is in progress. The output is reported as an
///            \c int16_t in hundredths of a degree Celsius so the API never
///            needs floating-point.
///
/// \author  galudino
/// \date    2026-05-16
/// \version 1.0 - Datasheet-derived DS3231 driver
///

#ifndef SENTINEL_DS3231_HPP
#define SENTINEL_DS3231_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_result.h"
}
#pragma GCC diagnostic pop

#include "sentinel_byte_transport.hpp"
#include "sentinel_utilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace sentinel {

template <typename Transport>
class ds3231;

} // namespace sentinel

///
/// \brief DS3231 real-time clock driver class
///
/// \details Provides a high-level C++ interface to the DS3231 I²C RTC.
///          The class exposes every datasheet feature as a typed member:
///          time/date read/write, dual alarms with five (Alarm 1) and four
///          (Alarm 2) match modes, square-wave / interrupt output on the
///          \c INT/SQW pin, the 32 kHz output, on-die temperature with
///          forced-conversion control, the aging-offset calibration
///          register, and a low-level raw register escape hatch.
///
///          The driver is I²C-only (the DS3231 has no SPI variant), so it
///          is constrained to transports tagged \ref sentinel::i2c_tag at
///          compile time.
///
/// \tparam Transport Transport implementation deriving from
///                   \c byte_transport<Transport, i2c_tag>.
///
template <typename Transport>
class sentinel::ds3231 {
    static_assert(
        std::is_base_of_v<byte_transport<Transport, i2c_tag>, Transport>,
        "Transport must derive from "
        "sentinel::byte_transport<Transport, i2c_tag>");

public:
    // =====================================================================
    // Error type
    // =====================================================================

    ///
    /// \brief Error codes reported by \ref last_error()
    ///
    /// \details Negative values are errors; \c ok (0) means the most
    ///          recent operation succeeded. The enum is \c int8_t-backed to
    ///          mirror the Bosch-style error code shape used elsewhere in
    ///          this codebase.
    ///
    enum class err : int8_t {
        ok = 0,                 ///< Most recent operation succeeded.
        transport_failure = -1, ///< Underlying I²C transport returned a
                                ///< non-success code.
        invalid_argument = -2,  ///< Input failed range validation (e.g.,
                                ///< month outside 1–12, hour > 23, year
                                ///< outside 2000–2199, etc.).
        busy_timeout = -3,      ///< Reserved for future use — the BSY
                                ///< flag did not clear within a polling
                                ///< budget.
    };

    // =====================================================================
    // Datasheet-derived constants
    // =====================================================================

    ///
    /// \brief I²C slave addresses for the DS3231
    ///
    /// \details The DS3231 has a single factory-fixed 7-bit address
    ///          (\c 0x68). There is no SDO / address-select pin.
    ///
    enum class slave_address : uint8_t {
        primary = 0x68,
    };

    ///
    /// \brief Complete DS3231 register map (datasheet section "Register Map")
    ///
    /// \details Every register documented in the DS3231 datasheet has a
    ///          named entry here, even ones the higher-level API does not
    ///          surface directly, so the map can be cross-checked against
    ///          the datasheet at a glance.
    ///
    enum class register_address : uint8_t {
        seconds = 0x00,         ///< Seconds (BCD 00–59).
        minutes = 0x01,         ///< Minutes (BCD 00–59).
        hours = 0x02,           ///< Hours (BCD); bit 6 selects 12/24-hour
                                ///< mode. This driver forces 24-hour mode.
        day_of_week = 0x03,     ///< Day of week (1–7, user-defined).
        date = 0x04,            ///< Date of month (BCD 01–31).
        month_century = 0x05,   ///< Month (BCD 01–12) in bits 4:0; century
                                ///< flag in bit 7 (0 → 2000s, 1 → 2100s).
        year = 0x06,            ///< Year within century (BCD 00–99).
        alarm1_seconds = 0x07,  ///< Alarm 1 seconds + A1M1 match bit.
        alarm1_minutes = 0x08,  ///< Alarm 1 minutes + A1M2 match bit.
        alarm1_hours = 0x09,    ///< Alarm 1 hours + A1M3 match bit.
        alarm1_day_date = 0x0A, ///< Alarm 1 day/date + A1M4 + DY/DT.
        alarm2_minutes = 0x0B,  ///< Alarm 2 minutes + A2M2 match bit.
        alarm2_hours = 0x0C,    ///< Alarm 2 hours + A2M3 match bit.
        alarm2_day_date = 0x0D, ///< Alarm 2 day/date + A2M4 + DY/DT.
        control = 0x0E,         ///< Control register (see
                                ///< \ref control_register).
        status = 0x0F,          ///< Status register (see
                                ///< \ref status_register).
        aging_offset = 0x10,    ///< Signed 8-bit oscillator aging offset.
        temperature_msb = 0x11, ///< Temperature integer part (signed 8-bit).
        temperature_lsb = 0x12, ///< Temperature fractional part
                                ///< (bits 7:6, 0.25 °C resolution).
    };

    ///
    /// \brief Control register (\c 0x0E) bit positions
    ///
    /// \details Bit name conventions follow the DS3231 datasheet exactly.
    ///
    struct control_register {
        static constexpr uint8_t EOSC_BIT = 7;  ///< Enable Oscillator: when
                                                ///< \c 1, oscillator stops
                                                ///< while running on VBAT.
                                                ///< \b Note: inverted sense.
        static constexpr uint8_t BBSQW_BIT = 6; ///< Battery-Backed Square
                                                ///< Wave Enable.
        static constexpr uint8_t CONV_BIT = 5;  ///< Convert Temperature
                                                ///< (one-shot, self-clearing).
        static constexpr uint8_t RS2_BIT = 4;   ///< Rate Select 2 (with
                                                ///< RS1, selects SQW freq).
        static constexpr uint8_t RS1_BIT = 3;   ///< Rate Select 1.
        static constexpr uint8_t INTCN_BIT = 2; ///< Interrupt Control:
                                                ///< \c 1 → INT mode,
                                                ///< \c 0 → SQW mode.
        static constexpr uint8_t A2IE_BIT = 1;  ///< Alarm 2 Interrupt Enable.
        static constexpr uint8_t A1IE_BIT = 0;  ///< Alarm 1 Interrupt Enable.
    };

    ///
    /// \brief Status register (\c 0x0F) bit positions
    ///
    struct status_register {
        static constexpr uint8_t OSF_BIT = 7;     ///< Oscillator Stop Flag
                                                  ///< (set on power-up; user
                                                  ///< clears after setting
                                                  ///< time).
        static constexpr uint8_t EN32KHZ_BIT = 3; ///< 32 kHz Output Enable.
        static constexpr uint8_t BSY_BIT = 2;     ///< Busy (TCXO functions
                                                  ///< or temp conversion in
                                                  ///< progress).
        static constexpr uint8_t A2F_BIT = 1;     ///< Alarm 2 triggered flag.
        static constexpr uint8_t A1F_BIT = 0;     ///< Alarm 1 triggered flag.
    };

    ///
    /// \brief Square-wave output frequencies (RS2:RS1 in control register)
    ///
    /// \details Only effective when \ref int_sqw_mode::square_wave is
    ///          selected on the INT/SQW pin.
    ///
    enum class square_wave_freq : uint8_t {
        hz_1 = 0b00,      ///<   1 Hz.
        khz_1_024 = 0b01, ///< 1.024 kHz.
        khz_4_096 = 0b10, ///< 4.096 kHz.
        khz_8_192 = 0b11, ///< 8.192 kHz.
    };

    ///
    /// \brief INT/SQW pin function (INTCN in control register)
    ///
    enum class int_sqw_mode : uint8_t {
        square_wave = 0, ///< Pin outputs the configured square wave.
        interrupt = 1,   ///< Pin is asserted low when an enabled alarm fires.
    };

    ///
    /// \brief ISO 8601 day-of-week convention adopted by this driver
    ///
    /// \details The DS3231 itself stores the day-of-week as an opaque 1–7
    ///          integer. This driver fixes the mapping to ISO 8601 so the
    ///          field is interoperable with conventional date arithmetic.
    ///
    enum class day_of_week : uint8_t {
        monday = 1,
        tuesday = 2,
        wednesday = 3,
        thursday = 4,
        friday = 5,
        saturday = 6,
        sunday = 7,
    };

    ///
    /// \brief Alarm 1 match modes (A1M4 / A1M3 / A1M2 / A1M1 / DY-DT)
    ///
    /// \details Mirrors the alarm-match table in the DS3231 datasheet.
    ///          \ref once_per_second fires every second regardless of the
    ///          payload fields; the rest progressively narrow the trigger
    ///          condition.
    ///
    enum class alarm1_match_mode : uint8_t {
        once_per_second,                   ///< A1M4:A1M1 = 1111.
        seconds,                           ///< A1M4:A1M1 = 1110.
        minutes_seconds,                   ///< A1M4:A1M1 = 1100.
        hours_minutes_seconds,             ///< A1M4:A1M1 = 1000.
        date_hours_minutes_seconds,        ///< A1M4:A1M1 = 0000, DY/DT = 0.
        day_of_week_hours_minutes_seconds, ///< A1M4:A1M1 = 0000, DY/DT = 1.
    };

    ///
    /// \brief Alarm 2 match modes (A2M4 / A2M3 / A2M2 / DY-DT)
    ///
    /// \details Alarm 2 has no seconds field — it always fires at the
    ///          0-th second of a matching minute.
    ///
    enum class alarm2_match_mode : uint8_t {
        once_per_minute,           ///< A2M4:A2M2 = 111.
        minutes,                   ///< A2M4:A2M2 = 110.
        hours_minutes,             ///< A2M4:A2M2 = 100.
        date_hours_minutes,        ///< A2M4:A2M2 = 000, DY/DT = 0.
        day_of_week_hours_minutes, ///< A2M4:A2M2 = 000, DY/DT = 1.
    };

    // =====================================================================
    // Domain types
    // =====================================================================

    ///
    /// \brief Conventional date/time payload (UTC) used by time-of-day
    ///        getters/setters and by both alarms.
    ///
    /// \details All fields are stored as plain integers; \ref to_unix_time
    ///          and \ref from_unix_time perform the Gregorian conversion
    ///          (UTC) for the 2000-01-01 → 2099-12-31 range that the DS3231
    ///          can natively address. Construction defaults to the DS3231
    ///          epoch (2000-01-01 00:00:00, a Saturday).
    ///
    struct datetime {
        uint16_t year;       ///< Four-digit year, 2000–2199.
        uint8_t month;       ///< Month, 1–12.
        uint8_t date;        ///< Date of month, 1–31.
        uint8_t day_of_week; ///< ISO 8601 day-of-week, 1=Mon … 7=Sun.
        uint8_t hour;        ///< Hour, 0–23 (24-hour clock).
        uint8_t minute;      ///< Minute, 0–59.
        uint8_t second;      ///< Second, 0–59.

        ///
        /// \brief Default-construct as 2000-01-01 00:00:00 (Saturday)
        ///
        /// \details 2000-01-01 was a Saturday, which encodes as ISO
        ///          day-of-week \c 6.
        ///
        datetime() noexcept
            : year(2000), month(1), date(1),
              day_of_week(static_cast<uint8_t>(ds3231::day_of_week::saturday)),
              hour(0), minute(0), second(0) {}

        ///
        /// \brief Validate field ranges against the DS3231-addressable
        ///        date space.
        ///
        /// \return \c true if every field is within its allowed range and
        ///         the date is a valid calendar day for the given month
        ///         (including leap-year handling for February).
        ///
        bool is_valid() const noexcept {
            if (year < 2000 || year > 2199) {
                return false;
            }

            if (month < 1 || month > 12) {
                return false;
            }

            if (date < 1 || date > 31) {
                return false;
            }

            if (day_of_week < 1 || day_of_week > 7) {
                return false;
            }

            if (hour > 23) {
                return false;
            }

            if (minute > 59) {
                return false;
            }

            if (second > 59) {
                return false;
            }

            return date <= days_in_month(year, month);
        }

        ///
        /// \brief Convert a UTC datetime to a Unix timestamp.
        ///
        /// \details Returns seconds since 1970-01-01 00:00:00 UTC. The
        ///          DS3231 cannot natively represent dates before 2000, so
        ///          \p dt must lie in the 2000-01-01 → 2199-12-31 range.
        ///
        /// \param dt Date/time to convert. Must satisfy \ref is_valid.
        /// \return Unix timestamp on success; \c std::nullopt if \p dt is
        ///         out of range or otherwise invalid.
        ///
        static std::optional<uint32_t>
        to_unix_time(const datetime &dt) noexcept {
            if (!dt.is_valid()) {
                return std::nullopt;
            }

            auto days = uint32_t{0};
            for (auto y = uint16_t{1970}; y < dt.year; ++y) {
                days += is_leap_year(y) ? 366 : 365;
            }

            for (auto m = uint8_t{1}; m < dt.month; ++m) {
                days += days_in_month(dt.year, m);
            }

            days += static_cast<uint32_t>(dt.date - 1);

            return days * 86400u + static_cast<uint32_t>(dt.hour) * 3600u +
                   static_cast<uint32_t>(dt.minute) * 60u +
                   static_cast<uint32_t>(dt.second);
        }

        ///
        /// \brief Convert a Unix timestamp to a UTC datetime.
        ///
        /// \details The result's \c day_of_week is computed from the
        ///          known anchor that 1970-01-01 was a Thursday
        ///          (ISO day \c 4).
        ///
        /// \param unix_time Seconds since 1970-01-01 00:00:00 UTC.
        /// \return Equivalent datetime in UTC, or \c std::nullopt if the
        ///         input lies outside the DS3231-addressable range
        ///         (≥ 2000-01-01 and ≤ 2199-12-31 23:59:59).
        ///
        static std::optional<datetime>
        from_unix_time(uint32_t unix_time) noexcept {
            // 2000-01-01 00:00:00 UTC == 946684800.
            // 2200-01-01 00:00:00 UTC == 7258118400 (overflows uint32_t).
            // uint32_t maxes out 2106-02-07 06:28:15 UTC, which is well
            // inside the DS3231 range; no upper bound check needed.
            static constexpr auto unix_year_2000 = uint32_t{946684800};
            if (unix_time < unix_year_2000) {
                return std::nullopt;
            }

            auto days = unix_time / 86400u;
            auto seconds_in_day = unix_time % 86400u;

            auto out = datetime{};
            out.hour = static_cast<uint8_t>(seconds_in_day / 3600u);
            out.minute = static_cast<uint8_t>((seconds_in_day % 3600u) / 60u);
            out.second = static_cast<uint8_t>(seconds_in_day % 60u);

            // ISO day-of-week: 1970-01-01 was Thursday (4).
            out.day_of_week = static_cast<uint8_t>(((days + 3) % 7) + 1);

            auto year = uint16_t{1970};
            while (true) {
                auto in_year =
                    is_leap_year(year) ? uint32_t{366} : uint32_t{365};

                if (days < in_year) {
                    break;
                }

                days -= in_year;
                ++year;
            }
            out.year = year;

            auto month = uint8_t{1};
            while (month <= 12) {
                auto in_month =
                    static_cast<uint32_t>(days_in_month(year, month));

                if (days < in_month) {
                    break;
                }

                days -= in_month;
                ++month;
            }

            out.month = month;
            out.date = static_cast<uint8_t>(days + 1);

            return out;
        }

        ///
        /// \brief Test whether \p year is a Gregorian leap year.
        ///
        static constexpr bool is_leap_year(uint16_t year) noexcept {
            return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        }

        ///
        /// \brief Number of days in a given month, with leap-year handling.
        ///
        static constexpr uint8_t days_in_month(uint16_t year,
                                               uint8_t month) noexcept {
            constexpr auto table = std::array<uint8_t, 12>{
                31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            if (month < 1 || month > 12) {
                return 0;
            }

            if (month == 2 && is_leap_year(year)) {
                return 29;
            }

            return table[month - 1];
        }

        friend bool operator==(const datetime &a, const datetime &b) noexcept {
            return a.year == b.year && a.month == b.month && a.date == b.date &&
                   a.hour == b.hour && a.minute == b.minute &&
                   a.second == b.second;
            // day_of_week intentionally excluded: it is derivable from
            // year/month/date and not all callers populate it correctly.
        }

        friend bool operator!=(const datetime &a, const datetime &b) noexcept {
            return !(a == b);
        }
    };

    ///
    /// \brief Alarm 1 configuration payload (seconds-precision alarm)
    ///
    struct alarm1_setting {
        alarm1_match_mode match_mode = alarm1_match_mode::once_per_second;
        uint8_t second = 0;      ///< 0–59 (ignored for \c once_per_second).
        uint8_t minute = 0;      ///< 0–59.
        uint8_t hour = 0;        ///< 0–23 (24-hour clock).
        uint8_t day_or_date = 1; ///< Day-of-week (1–7) if the match mode is
                                 ///< \c day_of_week_hours_minutes_seconds,
                                 ///< otherwise date-of-month (1–31).
    };

    ///
    /// \brief Alarm 2 configuration payload (minute-precision alarm)
    ///
    struct alarm2_setting {
        alarm2_match_mode match_mode = alarm2_match_mode::once_per_minute;
        uint8_t minute = 0;      ///< 0–59 (ignored for \c once_per_minute).
        uint8_t hour = 0;        ///< 0–23 (24-hour clock).
        uint8_t day_or_date = 1; ///< Day-of-week (1–7) if the match mode is
                                 ///< \c day_of_week_hours_minutes,
                                 ///< otherwise date-of-month (1–31).
    };

    // =====================================================================
    // Construction
    // =====================================================================

    ///
    /// \brief Construct the driver and bind it to a transport.
    ///
    /// \details No bus traffic is issued during construction; the DS3231
    ///          has no chip-ID register to probe and the part runs the
    ///          oscillator continuously from VCC or VBAT regardless of
    ///          host involvement. To verify the part is responding after
    ///          construction, read \ref status or call \ref
    ///          oscillator_stop_flag.
    ///
    /// \param bus            Reference to the I²C transport. Must outlive
    ///                       this driver instance.
    /// \param device_address 7-bit I²C target address. Defaults to
    ///                       \c BME280_I2C_ADDR_PRIM-style
    ///                       \c slave_address::primary (\c 0x68). Override
    ///                       only if address-translation hardware is in
    ///                       the path; the DS3231 itself does not have an
    ///                       address-select pin.
    ///
    explicit ds3231(Transport &bus,
                    uint16_t device_address =
                        static_cast<uint16_t>(slave_address::primary)) noexcept
        : m_bus(bus) {
        m_bus.set_target_address(device_address);
    }

    /// Non-copyable: holds a non-owning reference and a per-instance error
    /// cache; copying does not have a useful interpretation.
    ds3231(const ds3231 &) = delete;
    ds3231 &operator=(const ds3231 &) = delete;

    /// Movable.
    ds3231(ds3231 &&) noexcept = default;
    ds3231 &operator=(ds3231 &&) noexcept = default;

    // =====================================================================
    // Forensic accessor
    // =====================================================================

    ///
    /// \brief Return the error code from the most recent operation.
    ///
    /// \return \ref err::ok if the last call succeeded; otherwise the
    ///         specific error that caused the most recent failure.
    ///
    err last_error() const noexcept { return m_last_error; }

    // =====================================================================
    // Time / date
    // =====================================================================

    ///
    /// \brief Read the current date/time from registers \c 0x00 – \c 0x06.
    ///
    /// \return Current date/time on success, or \c std::nullopt on bus
    ///         error. The hour field is always returned in 24-hour form.
    ///
    /// \note If the DS3231 was running in 12-hour mode prior to driver
    ///       use, the first \ref set_time call switches it to 24-hour mode.
    ///       Until then this method correctly decodes both modes.
    ///
    std::optional<datetime> time() const noexcept {
        auto buf = std::array<uint8_t, 7>{};
        if (!read_registers(register_address::seconds, buf.data(),
                            buf.size())) {
            return std::nullopt;
        }

        auto dt = datetime{};
        dt.second = bcd_to_binary(buf[0] & 0x7F);
        dt.minute = bcd_to_binary(buf[1] & 0x7F);
        dt.hour = decode_hours(buf[2]);
        dt.day_of_week = static_cast<uint8_t>(buf[3] & 0x07);
        dt.date = bcd_to_binary(buf[4] & 0x3F);

        auto month_bcd = static_cast<uint8_t>(buf[5] & 0x1F);
        auto century = static_cast<uint16_t>((buf[5] & 0x80) ? 2100 : 2000);
        dt.month = bcd_to_binary(month_bcd);
        dt.year = century + bcd_to_binary(buf[6]);

        m_last_error = err::ok;
        return dt;
    }

    ///
    /// \brief Set the current date/time in registers \c 0x00 – \c 0x06.
    ///
    /// \details The hours register is always written in 24-hour mode
    ///          (bit 6 cleared). The seconds register is written with
    ///          bit 7 = 0 (the DS3231 datasheet specifies that bit as
    ///          reserved-zero).
    ///
    /// \param dt Date/time to program. Must satisfy
    ///           \ref datetime::is_valid.
    /// \return \c true on success. On failure, \ref last_error()
    ///         distinguishes a bus error (\ref err::transport_failure)
    ///         from a validation failure (\ref err::invalid_argument).
    ///
    /// \note This method does not clear the oscillator-stop flag (OSF).
    ///       After successfully calling \ref set_time, callers typically
    ///       want to follow up with \ref clear_oscillator_stop_flag so
    ///       a subsequent \ref oscillator_stop_flag reads \c false.
    ///
    bool set_time(const datetime &dt) noexcept {
        if (!dt.is_valid()) {
            m_last_error = err::invalid_argument;
            return false;
        }

        auto buf = std::array<uint8_t, 7>{};
        buf[0] = binary_to_bcd(dt.second);
        buf[1] = binary_to_bcd(dt.minute);
        buf[2] = binary_to_bcd(dt.hour); // bit 6 = 0 → 24-hour mode.
        buf[3] = static_cast<uint8_t>(dt.day_of_week & 0x07);
        buf[4] = binary_to_bcd(dt.date);

        auto month_bcd = binary_to_bcd(dt.month);
        auto century_bit = static_cast<uint8_t>(dt.year >= 2100 ? 0x80 : 0x00);
        buf[5] = static_cast<uint8_t>(month_bcd | century_bit);
        buf[6] = binary_to_bcd(
            static_cast<uint8_t>(dt.year - (dt.year >= 2100 ? 2100 : 2000)));

        return write_registers(register_address::seconds, buf.data(),
                               buf.size());
    }

    ///
    /// \brief Convenience: read the current time and return it as a Unix
    ///        timestamp.
    ///
    /// \return Unix seconds on success; \c std::nullopt on bus error or
    ///         if the current date is outside the Unix-time-representable
    ///         range.
    ///
    std::optional<uint32_t> unix_time() const noexcept {
        auto dt = time();

        if (!dt) {
            return std::nullopt;
        }

        return datetime::to_unix_time(*dt);
    }

    ///
    /// \brief Convenience: set the current time from a Unix timestamp.
    ///
    /// \param unix_seconds Seconds since 1970-01-01 00:00:00 UTC. Must be
    ///                     ≥ 946684800 (2000-01-01) to be representable
    ///                     on the DS3231.
    /// \return \c true on success.
    ///
    bool set_unix_time(uint32_t unix_seconds) noexcept {
        auto dt = datetime::from_unix_time(unix_seconds);

        if (!dt) {
            m_last_error = err::invalid_argument;
            return false;
        }

        return set_time(*dt);
    }

    // =====================================================================
    // Temperature
    // =====================================================================

    ///
    /// \brief Read the on-die temperature.
    ///
    /// \details The DS3231 stores temperature in two registers: an 8-bit
    ///          signed integer part (\c 0x11) and a 2-bit fractional part
    ///          in the upper bits of \c 0x12 (each LSB == 0.25 °C). This
    ///          method returns the temperature in hundredths of a degree
    ///          Celsius as an \c int16_t so callers never have to invoke
    ///          a float formatter.
    ///
    /// \return Temperature in centi-°C (e.g., \c 2375 means 23.75 °C), or
    ///         \c std::nullopt on bus error.
    ///
    std::optional<int16_t> temperature_centi_c() const noexcept {
        auto buf = std::array<uint8_t, 2>{};

        if (!read_registers(register_address::temperature_msb, buf.data(),
                            buf.size())) {
            return std::nullopt;
        }

        auto integer = static_cast<int16_t>(static_cast<int8_t>(buf[0]));
        auto fraction = static_cast<int16_t>((buf[1] >> 6) & 0x03); // 0..3
        return static_cast<int16_t>(integer * 100 + fraction * 25);
    }

    ///
    /// \brief Trigger a user-forced temperature conversion (CONV bit).
    ///
    /// \details Setting CONV starts a new temperature conversion outside
    ///          the normal 64 s schedule. The bit self-clears when the
    ///          conversion completes; poll \ref is_temperature_conversion_busy
    ///          to wait. The datasheet specifies that the user must not
    ///          set CONV while the part is already busy.
    ///
    /// \return \c true if CONV was set; \c false if the device is already
    ///         busy (\c last_error == \ref err::busy_timeout) or on
    ///         bus error.
    ///
    bool start_temperature_conversion() noexcept {
        auto current_status = read_register(register_address::status);

        if (!current_status) {
            return false;
        }

        if (*current_status & (1u << status_register::BSY_BIT)) {
            m_last_error = err::busy_timeout;
            return false;
        }

        auto current_control = read_register(register_address::control);
        if (!current_control) {
            return false;
        }

        auto new_control = static_cast<uint8_t>(
            *current_control | (1u << control_register::CONV_BIT));

        return write_register(register_address::control, new_control);
    }

    ///
    /// \brief Poll the BSY flag in the status register.
    ///
    /// \return \c true if the DS3231 is currently performing a TCXO
    ///         function or a temperature conversion;
    ///         \c std::nullopt on bus error.
    ///
    std::optional<bool> is_temperature_conversion_busy() const noexcept {
        auto s = read_register(register_address::status);
        if (!s) {
            return std::nullopt;
        }

        return (*s & (1u << status_register::BSY_BIT)) != 0;
    }

    // =====================================================================
    // Control register
    // =====================================================================

    ///
    /// \brief Read the raw control register byte (\c 0x0E).
    ///
    std::optional<uint8_t> control() const noexcept {
        return read_register(register_address::control);
    }

    ///
    /// \brief Write the raw control register byte (\c 0x0E).
    ///
    bool set_control(uint8_t value) noexcept {
        return write_register(register_address::control, value);
    }

    ///
    /// \brief Enable or disable the oscillator while running on VBAT.
    ///
    /// \details The DS3231's EOSC bit has \e inverted sense: when EOSC=1
    ///          the oscillator stops on VBAT (saving power but losing
    ///          time); when EOSC=0 it continues to run. This method takes
    ///          the user-intuitive sense — \c true means "keep the
    ///          oscillator running."
    ///
    /// \param enabled \c true to keep the oscillator running on VBAT.
    /// \return \c true on success.
    ///
    bool set_oscillator_enabled_on_vbat(bool enabled) noexcept {
        return modify_control_bit(control_register::EOSC_BIT, !enabled);
    }

    ///
    /// \brief Enable or disable the battery-backed square wave (BBSQW).
    ///
    bool set_battery_backed_square_wave_enabled(bool enabled) noexcept {
        return modify_control_bit(control_register::BBSQW_BIT, enabled);
    }

    ///
    /// \brief Enable or disable the Alarm 1 interrupt (A1IE).
    ///
    bool set_alarm1_interrupt_enabled(bool enabled) noexcept {
        return modify_control_bit(control_register::A1IE_BIT, enabled);
    }

    ///
    /// \brief Enable or disable the Alarm 2 interrupt (A2IE).
    ///
    bool set_alarm2_interrupt_enabled(bool enabled) noexcept {
        return modify_control_bit(control_register::A2IE_BIT, enabled);
    }

    ///
    /// \brief Configure the function of the INT/SQW pin (INTCN).
    ///
    bool set_int_sqw_mode(int_sqw_mode mode) noexcept {
        return modify_control_bit(control_register::INTCN_BIT,
                                  mode == int_sqw_mode::interrupt);
    }

    ///
    /// \brief Select the square-wave output frequency (RS2:RS1).
    ///
    /// \details This affects the INT/SQW pin only when
    ///          \ref set_int_sqw_mode is set to
    ///          \ref int_sqw_mode::square_wave.
    ///
    bool set_square_wave_freq(square_wave_freq f) noexcept {
        auto ctrl = read_register(register_address::control);
        if (!ctrl) {
            return false;
        }

        auto v =
            static_cast<uint8_t>(*ctrl & ~((1u << control_register::RS2_BIT) |
                                           (1u << control_register::RS1_BIT)));
        v = static_cast<uint8_t>(
            v | (static_cast<uint8_t>(f) << control_register::RS1_BIT));

        return write_register(register_address::control, v);
    }

    // =====================================================================
    // Status register
    // =====================================================================

    ///
    /// \brief Read the raw status register byte (\c 0x0F).
    ///
    std::optional<uint8_t> status() const noexcept {
        return read_register(register_address::status);
    }

    ///
    /// \brief Read the oscillator-stop flag (OSF).
    ///
    /// \details OSF is set by the DS3231 whenever the oscillator was, or
    ///          is, stopped — for instance on first power-up or after a
    ///          VCC + VBAT power loss. The flag remains set until the
    ///          user explicitly clears it via
    ///          \ref clear_oscillator_stop_flag.
    ///
    /// \return \c true if OSF is set (time may be invalid);
    ///         \c std::nullopt on bus error.
    ///
    std::optional<bool> oscillator_stop_flag() const noexcept {
        auto s = read_register(register_address::status);
        if (!s) {
            return std::nullopt;
        }

        return (*s & (1u << status_register::OSF_BIT)) != 0;
    }

    ///
    /// \brief Clear the oscillator-stop flag (OSF = 0).
    ///
    bool clear_oscillator_stop_flag() noexcept {
        return modify_status_bit(status_register::OSF_BIT, false);
    }

    ///
    /// \brief Read the Alarm 1 triggered flag (A1F).
    ///
    std::optional<bool> alarm1_triggered() const noexcept {
        auto s = read_register(register_address::status);
        if (!s) {
            return std::nullopt;
        }

        return (*s & (1u << status_register::A1F_BIT)) != 0;
    }

    ///
    /// \brief Clear the Alarm 1 triggered flag (A1F = 0).
    ///
    bool clear_alarm1_flag() noexcept {
        return modify_status_bit(status_register::A1F_BIT, false);
    }

    ///
    /// \brief Read the Alarm 2 triggered flag (A2F).
    ///
    std::optional<bool> alarm2_triggered() const noexcept {
        auto s = read_register(register_address::status);
        if (!s) {
            return std::nullopt;
        }

        return (*s & (1u << status_register::A2F_BIT)) != 0;
    }

    ///
    /// \brief Clear the Alarm 2 triggered flag (A2F = 0).
    ///
    bool clear_alarm2_flag() noexcept {
        return modify_status_bit(status_register::A2F_BIT, false);
    }

    ///
    /// \brief Enable or disable the 32 kHz square-wave output (EN32KHZ).
    ///
    bool set_32khz_output_enabled(bool enabled) noexcept {
        return modify_status_bit(status_register::EN32KHZ_BIT, enabled);
    }

    ///
    /// \brief Read the 32 kHz output enable bit (EN32KHZ).
    ///
    std::optional<bool> is_32khz_output_enabled() const noexcept {
        auto s = read_register(register_address::status);
        if (!s) {
            return std::nullopt;
        }

        return (*s & (1u << status_register::EN32KHZ_BIT)) != 0;
    }

    // =====================================================================
    // Alarms
    // =====================================================================

    ///
    /// \brief Configure Alarm 1 (registers \c 0x07 – \c 0x0A).
    ///
    /// \details Programmes the alarm registers and the match-mode bits
    ///          (A1M4..A1M1, DY/DT). Does not modify the A1IE bit in the
    ///          control register; enable the interrupt separately via
    ///          \ref set_alarm1_interrupt_enabled.
    ///
    /// \param alarm Alarm configuration.
    /// \return \c true on success.
    ///
    bool set_alarm1(const alarm1_setting &alarm) noexcept {
        // Field validation (only the fields the chosen match mode
        // actually consults).
        if (alarm.second > 59 || alarm.minute > 59 || alarm.hour > 23) {
            m_last_error = err::invalid_argument;
            return false;
        }

        if (alarm.match_mode ==
            alarm1_match_mode::day_of_week_hours_minutes_seconds) {
            if (alarm.day_or_date < 1 || alarm.day_or_date > 7) {
                m_last_error = err::invalid_argument;
                return false;
            }
        } else if (alarm.match_mode ==
                   alarm1_match_mode::date_hours_minutes_seconds) {
            if (alarm.day_or_date < 1 || alarm.day_or_date > 31) {
                m_last_error = err::invalid_argument;
                return false;
            }
        }

        auto flags = alarm1_match_flags(alarm.match_mode);
        auto buf = std::array<uint8_t, 4>{};

        buf[0] = static_cast<uint8_t>((flags.m1 ? 0x80 : 0x00) |
                                      binary_to_bcd(alarm.second));
        buf[1] = static_cast<uint8_t>((flags.m2 ? 0x80 : 0x00) |
                                      binary_to_bcd(alarm.minute));
        buf[2] = static_cast<uint8_t>((flags.m3 ? 0x80 : 0x00) |
                                      binary_to_bcd(alarm.hour));
        buf[3] = static_cast<uint8_t>((flags.m4 ? 0x80 : 0x00) |
                                      (flags.day_not_date ? 0x40 : 0x00) |
                                      binary_to_bcd(alarm.day_or_date));

        return write_registers(register_address::alarm1_seconds, buf.data(),
                               buf.size());
    }

    ///
    /// \brief Read Alarm 1 configuration (registers \c 0x07 – \c 0x0A).
    ///
    std::optional<alarm1_setting> alarm1() const noexcept {
        auto buf = std::array<uint8_t, 4>{};
        if (!read_registers(register_address::alarm1_seconds, buf.data(),
                            buf.size())) {
            return std::nullopt;
        }

        auto m1 = (buf[0] & 0x80) != 0;
        auto m2 = (buf[1] & 0x80) != 0;
        auto m3 = (buf[2] & 0x80) != 0;
        auto m4 = (buf[3] & 0x80) != 0;
        auto dy = (buf[3] & 0x40) != 0;

        auto out = alarm1_setting{};
        out.match_mode = alarm1_mode_from_flags(m1, m2, m3, m4, dy);
        out.second = bcd_to_binary(buf[0] & 0x7F);
        out.minute = bcd_to_binary(buf[1] & 0x7F);
        out.hour = decode_hours(buf[2]);
        out.day_or_date =
            bcd_to_binary(static_cast<uint8_t>(buf[3] & (dy ? 0x0F : 0x3F)));

        return out;
    }

    ///
    /// \brief Configure Alarm 2 (registers \c 0x0B – \c 0x0D).
    ///
    bool set_alarm2(const alarm2_setting &alarm) noexcept {
        if (alarm.minute > 59 || alarm.hour > 23) {
            m_last_error = err::invalid_argument;
            return false;
        }

        if (alarm.match_mode == alarm2_match_mode::day_of_week_hours_minutes) {
            if (alarm.day_or_date < 1 || alarm.day_or_date > 7) {
                m_last_error = err::invalid_argument;
                return false;
            }
        } else if (alarm.match_mode == alarm2_match_mode::date_hours_minutes) {
            if (alarm.day_or_date < 1 || alarm.day_or_date > 31) {
                m_last_error = err::invalid_argument;
                return false;
            }
        }

        auto flags = alarm2_match_flags(alarm.match_mode);
        auto buf = std::array<uint8_t, 3>{};

        buf[0] = static_cast<uint8_t>((flags.m2 ? 0x80 : 0x00) |
                                      binary_to_bcd(alarm.minute));
        buf[1] = static_cast<uint8_t>((flags.m3 ? 0x80 : 0x00) |
                                      binary_to_bcd(alarm.hour));
        buf[2] = static_cast<uint8_t>((flags.m4 ? 0x80 : 0x00) |
                                      (flags.day_not_date ? 0x40 : 0x00) |
                                      binary_to_bcd(alarm.day_or_date));

        return write_registers(register_address::alarm2_minutes, buf.data(),
                               buf.size());
    }

    ///
    /// \brief Read Alarm 2 configuration (registers \c 0x0B – \c 0x0D).
    ///
    std::optional<alarm2_setting> alarm2() const noexcept {
        auto buf = std::array<uint8_t, 3>{};
        if (!read_registers(register_address::alarm2_minutes, buf.data(),
                            buf.size())) {
            return std::nullopt;
        }

        auto m2 = (buf[0] & 0x80) != 0;
        auto m3 = (buf[1] & 0x80) != 0;
        auto m4 = (buf[2] & 0x80) != 0;
        auto dy = (buf[2] & 0x40) != 0;

        auto out = alarm2_setting{};

        out.match_mode = alarm2_mode_from_flags(m2, m3, m4, dy);
        out.minute = bcd_to_binary(buf[0] & 0x7F);
        out.hour = decode_hours(buf[1]);
        out.day_or_date =
            bcd_to_binary(static_cast<uint8_t>(buf[2] & (dy ? 0x0F : 0x3F)));

        return out;
    }

    // =====================================================================
    // Aging offset
    // =====================================================================

    ///
    /// \brief Read the aging-offset register (\c 0x10).
    ///
    /// \details The DS3231 oscillator-trim register is a signed 8-bit
    ///          value (-128 … +127) that applies a small frequency
    ///          correction. One LSB is approximately \c 0.1 ppm at +25 °C.
    ///
    /// \return Signed aging offset, or \c std::nullopt on bus error.
    ///
    std::optional<int8_t> aging_offset() const noexcept {
        auto v = read_register(register_address::aging_offset);
        if (!v)
            return std::nullopt;
        return static_cast<int8_t>(*v);
    }

    ///
    /// \brief Write the aging-offset register (\c 0x10).
    ///
    /// \details The new value takes effect at the next temperature
    ///          conversion. To force the new offset to apply immediately,
    ///          follow this call with \ref start_temperature_conversion.
    ///
    bool set_aging_offset(int8_t offset) noexcept {
        return write_register(register_address::aging_offset,
                              static_cast<uint8_t>(offset));
    }

    // =====================================================================
    // Low-level raw register access (escape hatch)
    // =====================================================================

    ///
    /// \brief Read a single named register.
    ///
    std::optional<uint8_t> read_register(register_address reg) const noexcept {
        auto v = uint8_t{};
        if (!read_register(reg, v)) {
            return std::nullopt;
        }
        return v;
    }

    ///
    /// \brief Write a single named register.
    ///
    /// \details The DS3231 has no protected registers — every byte from
    ///          \c 0x00 through \c 0x12 is freely writeable — so this
    ///          provides a complete escape hatch for callers that need
    ///          register-level access beyond the typed API.
    ///
    bool write_register(register_address reg, uint8_t value) noexcept {
        m_last_error = err::ok;
        auto buf = std::array<uint8_t, 2>{static_cast<uint8_t>(reg), value};
        auto status_code = m_bus.write(buf.data(), buf.size(), 0, true);

        if (status_code != CY_RSLT_SUCCESS) {
            m_last_error = err::transport_failure;
            return false;
        }

        return true;
    }

private:
    // =====================================================================
    // BCD helpers
    // =====================================================================

    static constexpr uint8_t bcd_to_binary(uint8_t bcd) noexcept {
        return static_cast<uint8_t>(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F));
    }

    static constexpr uint8_t binary_to_bcd(uint8_t binary) noexcept {
        return static_cast<uint8_t>(((binary / 10) << 4) | (binary % 10));
    }

    ///
    /// \brief Decode an hours-register byte to a 24-hour value.
    ///
    /// \details Handles both 12-hour and 24-hour modes so an unexpected
    ///          chip state (e.g., a DS3231 left in 12-hour mode by some
    ///          other host) still produces a sensible result. After the
    ///          first \ref set_time call this driver leaves the part in
    ///          24-hour mode.
    ///
    static constexpr uint8_t decode_hours(uint8_t reg) noexcept {
        if (reg & 0x40) {
            // 12-hour mode: bit 5 is PM, bits 4:0 carry the hour in BCD.
            auto pm = (reg & 0x20) != 0;
            auto hour = bcd_to_binary(static_cast<uint8_t>(reg & 0x1F));

            if (hour == 12) {
                hour = 0; // 12 AM == 00; 12 PM == 12.
            }

            return pm ? static_cast<uint8_t>(hour + 12) : hour;
        }
        return bcd_to_binary(static_cast<uint8_t>(reg & 0x3F));
    }

    // =====================================================================
    // Alarm match-mode flag packing
    // =====================================================================

    struct match_flags {
        bool m1;
        bool m2;
        bool m3;
        bool m4;
        bool day_not_date;
    };

    static constexpr match_flags
    alarm1_match_flags(alarm1_match_mode m) noexcept {
        switch (m) {
        case alarm1_match_mode::once_per_second:
            return {true, true, true, true, false};
        case alarm1_match_mode::seconds:
            return {false, true, true, true, false};
        case alarm1_match_mode::minutes_seconds:
            return {false, false, true, true, false};
        case alarm1_match_mode::hours_minutes_seconds:
            return {false, false, false, true, false};
        case alarm1_match_mode::date_hours_minutes_seconds:
            return {false, false, false, false, false};
        case alarm1_match_mode::day_of_week_hours_minutes_seconds:
            return {false, false, false, false, true};
        }
        return {true, true, true, true, false};
    }

    static constexpr match_flags
    alarm2_match_flags(alarm2_match_mode m) noexcept {
        // Alarm 2 has no seconds field; m1 is unused.
        switch (m) {
        case alarm2_match_mode::once_per_minute:
            return {false, true, true, true, false};
        case alarm2_match_mode::minutes:
            return {false, false, true, true, false};
        case alarm2_match_mode::hours_minutes:
            return {false, false, false, true, false};
        case alarm2_match_mode::date_hours_minutes:
            return {false, false, false, false, false};
        case alarm2_match_mode::day_of_week_hours_minutes:
            return {false, false, false, false, true};
        }
        return {false, true, true, true, false};
    }

    static constexpr alarm1_match_mode
    alarm1_mode_from_flags(bool m1, bool m2, bool m3, bool m4,
                           bool day_not_date) noexcept {
        if (m1 && m2 && m3 && m4)
            return alarm1_match_mode::once_per_second;
        if (!m1 && m2 && m3 && m4)
            return alarm1_match_mode::seconds;
        if (!m1 && !m2 && m3 && m4)
            return alarm1_match_mode::minutes_seconds;
        if (!m1 && !m2 && !m3 && m4)
            return alarm1_match_mode::hours_minutes_seconds;
        if (day_not_date)
            return alarm1_match_mode::day_of_week_hours_minutes_seconds;
        return alarm1_match_mode::date_hours_minutes_seconds;
    }

    static constexpr alarm2_match_mode
    alarm2_mode_from_flags(bool m2, bool m3, bool m4,
                           bool day_not_date) noexcept {
        if (m2 && m3 && m4)
            return alarm2_match_mode::once_per_minute;
        if (!m2 && m3 && m4)
            return alarm2_match_mode::minutes;
        if (!m2 && !m3 && m4)
            return alarm2_match_mode::hours_minutes;
        if (day_not_date)
            return alarm2_match_mode::day_of_week_hours_minutes;
        return alarm2_match_mode::date_hours_minutes;
    }

    // =====================================================================
    // Read-modify-write helpers for single-bit operations
    // =====================================================================

    bool modify_control_bit(uint8_t bit_position, bool set) noexcept {
        auto current = read_register(register_address::control);
        if (!current) {
            return false;
        }

        auto v = *current;
        if (set) {
            v = static_cast<uint8_t>(v | (1u << bit_position));
        } else {
            v = static_cast<uint8_t>(v & ~(1u << bit_position));
        }

        return write_register(register_address::control, v);
    }

    bool modify_status_bit(uint8_t bit_position, bool set) noexcept {
        auto current = read_register(register_address::status);
        if (!current) {
            return false;
        }

        auto v = *current;
        if (set) {
            v = static_cast<uint8_t>(v | (1u << bit_position));
        } else {
            v = static_cast<uint8_t>(v & ~(1u << bit_position));
        }

        return write_register(register_address::status, v);
    }

    // =====================================================================
    // Low-level transport primitives
    // =====================================================================

    bool read_register(register_address reg, uint8_t &out) const noexcept {
        return read_registers(reg, &out, 1);
    }

    bool read_registers(register_address reg, uint8_t *out,
                        size_t count) const noexcept {
        m_last_error = err::ok;
        auto reg_byte = static_cast<uint8_t>(reg);
        auto status_code = m_bus.write_read(&reg_byte, sizeof(reg_byte), out,
                                            count, 0, 0, false, true);

        if (status_code != CY_RSLT_SUCCESS) {
            m_last_error = err::transport_failure;
            return false;
        }
        return true;
    }

    bool write_registers(register_address reg, const uint8_t *data,
                         size_t count) noexcept {
        // The DS3231 has no auto-increment write opcode; the standard
        // multi-byte write is a single transaction containing the starting
        // register address followed by N data bytes. We build that
        // transaction in a small stack buffer sized to the worst case
        // we ever write (7 bytes for the time/date block + 1 address byte
        // = 8 bytes; the 4-byte Alarm 1 block + 1 = 5; etc.). A 16-byte
        // cap is comfortably above every documented contiguous write.
        constexpr size_t MAX_CONTIGUOUS_BYTES = 16;
        if (count + 1 > MAX_CONTIGUOUS_BYTES) {
            m_last_error = err::invalid_argument;
            return false;
        }

        auto buf = std::array<uint8_t, MAX_CONTIGUOUS_BYTES>{};
        buf[0] = static_cast<uint8_t>(reg);
        for (auto i = size_t{0}; i < count; ++i) {
            buf[i + 1] = data[i];
        }

        m_last_error = err::ok;
        auto status_code = m_bus.write(buf.data(), count + 1, 0, true);
        if (status_code != CY_RSLT_SUCCESS) {
            m_last_error = err::transport_failure;
            return false;
        }
        return true;
    }

    Transport &m_bus;                  ///< Non-owning reference to the I²C
                                       ///< transport.
    mutable err m_last_error{err::ok}; ///< Cache of the most recent
                                       ///< operation's error code; exposed
                                       ///< by \ref last_error().
};

#endif /* SENTINEL_DS3231_HPP */
