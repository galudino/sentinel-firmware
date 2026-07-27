///
/// \file    sentinel_psoc6_die_temperature.cpp
/// \brief   PSoC 6 SAR-ADC die-temperature driver implementation
///
/// \details SAR configuration + conversion follow Infineon's DieTemp reference
///          (single-ended DieTemp channel routed via \c
///          CY_SAR_MUX_FW_TEMP_VPLUS, 1.2 V bandgap reference, 32× averaging)
///          and the canonical dual-slope counts→°C algorithm keyed off the
///          per-part SFLASH calibration
///          (\c SFLASH->SAR_TEMP_MULTIPLIER / \c SAR_TEMP_OFFSET). See the
///          header and the tracking issue; the calibration + SAR clock divider
///          need on-bench validation.
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cybsp.h"

#include "cy_sar.h"
#include "cy_sysanalog.h"
#include "cy_sysclk.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include "sentinel_psoc6_die_temperature.hpp"

#include <cstdint>

namespace sentinel::drivers {

namespace {

/// \brief 8-bit peripheral divider for the SAR clock. The BSP allocates 8-bit
///        dividers 0–4, so 6 is free; adjust if a future BSP change claims it.
constexpr uint32_t SAR_CLK_DIVIDER_NUM = 6u;

/// \brief Target SAR clock (~8 MHz): well under the 18 MHz max, and the fixed
///        20-cycle sample time below then comfortably exceeds the DieTemp
///        sensor's ≥1 µs settling requirement.
constexpr uint32_t SAR_TARGET_CLK_HZ = 8'000'000u;

/// \brief Sample time (SAR clocks) for the DieTemp channel. ≥1 µs at any SAR
///        clock ≤ ~16 MHz.
constexpr uint32_t SAR_SAMPLE_TIME0_CYCLES = 20u;

/// \brief Minimum spacing between conversions (ms) — the throttle window.
constexpr uint32_t REFRESH_MIN_INTERVAL_MS = 1000u;

/// \brief Bounded poll iterations while waiting for end-of-conversion.
constexpr uint32_t CONVERT_POLL_LIMIT = 100000u;

/// \brief DieTemp channel 0: single-ended, SARMUX virtual port, hardware avg.
constexpr uint32_t CHAN0_CONFIG =
    CY_SAR_CHAN_SINGLE_ENDED | CY_SAR_CHAN_SAMPLE_TIME_0 |
    CY_SAR_POS_PORT_ADDR_SARMUX_VIRT | CY_SAR_CHAN_POS_PIN_ADDR_0 |
    CY_SAR_CHAN_AVG_ENABLE;

/// \brief Build the SAR configuration for a single DieTemp-sensor conversion.
///
/// \details Value-initialized (zeros every field, incl. FIFO / deep-sleep
/// fields
///          unused here) then the DieTemp fields are set — this avoids C++20
///          designated initializers (the project is C++17 + \c
///          -pedantic-errors).
/// \return A fully populated \c cy_stc_sar_config_t for the DieTemp channel.
cy_stc_sar_config_t make_die_temp_config() noexcept {
    cy_stc_sar_config_t c{};
    c.ctrl = CY_SAR_VREF_SEL_BGR | CY_SAR_BYPASS_CAP_ENABLE |
             CY_SAR_NEG_SEL_VSSA_KELVIN;
    c.sampleCtrl = CY_SAR_SINGLE_ENDED_SIGNED | CY_SAR_AVG_CNT_32 |
                   CY_SAR_AVG_MODE_SEQUENTIAL_FIXED;
    c.sampleTime01 = (SAR_SAMPLE_TIME0_CYCLES << CY_SAR_SAMPLE_TIME0_SHIFT) |
                     (4UL << CY_SAR_SAMPLE_TIME1_SHIFT);
    c.sampleTime23 =
        (4UL << CY_SAR_SAMPLE_TIME2_SHIFT) | (4UL << CY_SAR_SAMPLE_TIME3_SHIFT);
    c.rangeCond = CY_SAR_RANGE_COND_BELOW;
    c.chanEn = 0x01UL; // channel 0 only
    c.chanConfig[0] = CHAN0_CONFIG;
    c.muxSwitch = CY_SAR_MUX_FW_VSSA_VMINUS | CY_SAR_MUX_FW_TEMP_VPLUS;
    c.muxSwitchSqCtrl = CY_SAR_MUX_SQ_CTRL_VSSA | CY_SAR_MUX_SQ_CTRL_TEMP;
    c.configRouting = true;
    c.vrefMvValue = 1200UL;
    return c;
}

// ---- Dual-slope conversion constants (DieTemp component / TRM Ch. 39). ------
constexpr int32_t TEMP_OFFSET_MULT = 0x400;  ///< Offset scale factor.
constexpr int32_t Q16_ONE = 0x10000;         ///< 1.0 in Q16.16.
constexpr int32_t SCALE_ADJUSTMENT = 8;      ///< Dual-slope scale numerator.
constexpr int32_t SCALE_ADJUSTMENT_DIV = 16; ///< Dual-slope scale divisor.
constexpr int32_t DUAL_SLOPE_CORRECTION = 0xF0000; ///< 15.0 in Q16.16.
constexpr int32_t HIGH_TEMPERATURE = 0x640000;     ///< 100.0 in Q16.16.
constexpr int32_t LOW_TEMPERATURE = 0x280000;      ///< 40.0 in Q16.16.

} // namespace

psoc6_die_temperature &psoc6_die_temperature::instance() noexcept {
    static psoc6_die_temperature the_instance;
    return the_instance;
}

int16_t psoc6_die_temperature::counts_to_centi_c(int16_t adc_counts) noexcept {
    const int32_t offset_reg = static_cast<int16_t>(SFLASH->SAR_TEMP_OFFSET);
    const int32_t mult_reg = static_cast<int16_t>(SFLASH->SAR_TEMP_MULTIPLIER);

    // tInitial in Q16.16 degrees C.
    const int32_t t_initial =
        (adc_counts * mult_reg) + (offset_reg * TEMP_OFFSET_MULT);

    int32_t t_adjust;
    if (t_initial >= DUAL_SLOPE_CORRECTION) {
        t_adjust = (SCALE_ADJUSTMENT *
                    ((HIGH_TEMPERATURE - t_initial) / SCALE_ADJUSTMENT_DIV)) /
                   ((HIGH_TEMPERATURE - DUAL_SLOPE_CORRECTION) / Q16_ONE);
    } else {
        t_adjust = (SCALE_ADJUSTMENT *
                    ((LOW_TEMPERATURE + t_initial) / SCALE_ADJUSTMENT_DIV)) /
                   ((LOW_TEMPERATURE + DUAL_SLOPE_CORRECTION) / Q16_ONE);
    }

    // Q16.16 temperature → 0.01 °C, rounded to nearest centi-degree.
    const int32_t q16_celsius = t_initial + t_adjust;
    int32_t centi = static_cast<int32_t>(
        (static_cast<int64_t>(q16_celsius) * 100 + (Q16_ONE / 2)) / Q16_ONE);

    if (centi > INT16_MAX) {
        centi = INT16_MAX;
    } else if (centi < INT16_MIN) {
        centi = INT16_MIN;
    }
    return static_cast<int16_t>(centi);
}

bool psoc6_die_temperature::initialize() noexcept {
    if (m_initialized) {
        return true;
    }

    // Analog reference block required by the SAR.
    Cy_SysAnalog_Init(&Cy_SysAnalog_Fast_Local);
    Cy_SysAnalog_Enable();

    // SAR clock: divide PeriClk down to ~8 MHz on a free 8-bit divider.
    const uint32_t peri_hz = Cy_SysClk_ClkPeriGetFrequency();
    uint32_t ratio = (peri_hz + SAR_TARGET_CLK_HZ - 1u) / SAR_TARGET_CLK_HZ;
    if (ratio < 1u) {
        ratio = 1u;
    } else if (ratio > 256u) {
        ratio = 256u;
    }
    Cy_SysClk_PeriphAssignDivider(PCLK_PASS_CLOCK_SAR, CY_SYSCLK_DIV_8_BIT,
                                  SAR_CLK_DIVIDER_NUM);
    Cy_SysClk_PeriphSetDivider(CY_SYSCLK_DIV_8_BIT, SAR_CLK_DIVIDER_NUM,
                               (ratio - 1u) & 0xFFu);
    Cy_SysClk_PeriphEnableDivider(CY_SYSCLK_DIV_8_BIT, SAR_CLK_DIVIDER_NUM);

    const cy_stc_sar_config_t config = make_die_temp_config();
    if (Cy_SAR_Init(SAR, &config) != CY_SAR_SUCCESS) {
        return false;
    }
    Cy_SAR_Enable(SAR);

    m_mutex = xSemaphoreCreateMutex();
    if (m_mutex == nullptr) {
        return false;
    }

    m_initialized = true;
    return true;
}

bool psoc6_die_temperature::convert_once(int16_t &out_centi_c) noexcept {
    Cy_SAR_StartConvert(SAR, CY_SAR_START_CONVERT_SINGLE_SHOT);

    for (uint32_t i = 0; i < CONVERT_POLL_LIMIT; ++i) {
        if (Cy_SAR_IsEndConversion(SAR, CY_SAR_RETURN_STATUS) ==
            CY_SAR_SUCCESS) {
            out_centi_c = counts_to_centi_c(Cy_SAR_GetResult16(SAR, 0));
            return true;
        }
    }
    return false; // conversion did not complete in time.
}

void psoc6_die_temperature::refresh() noexcept {
    if (!m_initialized) {
        return;
    }

    const uint32_t now_ms =
        static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
    if (m_valid && (now_ms - m_last_refresh_tick) < REFRESH_MIN_INTERVAL_MS) {
        return;
    }

    // Zero timeout: if another producer is mid-conversion, skip rather than
    // block the snapshot populate path.
    if (xSemaphoreTake(m_mutex, 0) != pdTRUE) {
        return;
    }

    int16_t centi = 0;
    if (convert_once(centi)) {
        m_cached_centi_c = centi;
        m_valid = true;
        m_last_refresh_tick = now_ms;
    }

    xSemaphoreGive(m_mutex);
}

bool psoc6_die_temperature::cached_centi_c(
    int16_t &out_centi_c) const noexcept {
    if (!m_valid) {
        return false;
    }
    out_centi_c = m_cached_centi_c;
    return true;
}

} // namespace sentinel::drivers
