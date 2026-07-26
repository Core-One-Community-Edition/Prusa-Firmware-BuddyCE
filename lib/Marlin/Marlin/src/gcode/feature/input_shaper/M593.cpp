/**
 * Based on Marlin 3D Printer Firmware
 * Copyright (c) 2022 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 */
#include "../../../inc/MarlinConfig.h"

#include "../../gcode.h"
#include "../../../feature/input_shaper/input_shaper.hpp"
#include "../../../feature/input_shaper/input_shaper_config.hpp"
#include "../../../module/stepper.h"
#include "gcode/parser.h"
#include <config_store/store_instance.hpp>
#include <optional>

namespace input_shaper {

struct M593Params {
    bool seen_x;
    bool seen_y;
    bool seen_z;
    bool seen_w;
    struct {
        std::optional<Type> type;
        std::optional<float> frequency;
        std::optional<float> damping_ratio;
        std::optional<float> vibration_reduction;
    } axis;
    // Cascade second shaper parameters
    struct {
        std::optional<Type> type;
        std::optional<float> frequency;
        std::optional<float> damping_ratio;
        std::optional<float> vibration_reduction;
    } cascade;
    struct {
        std::optional<float> frequency_delta;
        std::optional<float> mass_limit;
    } weight_adjust;
};

static bool contains_axis_change(const M593Params &params) {
    return params.axis.type || params.axis.frequency || params.axis.damping_ratio || params.axis.vibration_reduction
        || params.cascade.type || params.cascade.frequency || params.cascade.damping_ratio || params.cascade.vibration_reduction;
}

static bool contains_weight_adjust_change(const M593Params &params) {
    return params.weight_adjust.frequency_delta || params.weight_adjust.mass_limit;
}

static bool is_empty(const M593Params &params) {
    return !contains_axis_change(params) && !contains_weight_adjust_change(params) && !params.seen_x && !params.seen_y && !params.seen_z && !params.seen_w;
}

static std::optional<AxisConfig> get_axis_config(const AxisConfig &config, const M593Params &params) {
    if (params.axis.frequency && *params.axis.frequency == 0) {
        return std::nullopt;
    }
    return AxisConfig {
        .type = params.axis.type.value_or(config.type),
        .frequency = params.axis.frequency.value_or(config.frequency),
        .damping_ratio = params.axis.damping_ratio.value_or(config.damping_ratio),
        .vibration_reduction = params.axis.vibration_reduction.value_or(config.vibration_reduction),
    };
}

/// Resolve cascade config from EEPROM default + M593 cascade params
static std::optional<AxisConfig> get_cascade_config(const std::optional<AxisConfig> &stored_cascade, const M593Params &params) {
    // If cascade frequency is explicitly set to 0, disable cascade
    if (params.cascade.frequency && *params.cascade.frequency == 0.f) {
        return std::nullopt;
    }

    const AxisConfig &base = stored_cascade.value_or(input_shaper::cascade_disabled_default);
    AxisConfig result {
        .type = params.cascade.type.value_or(base.type),
        .frequency = params.cascade.frequency.value_or(base.frequency),
        .damping_ratio = params.cascade.damping_ratio.value_or(base.damping_ratio),
        .vibration_reduction = params.cascade.vibration_reduction.value_or(base.vibration_reduction),
    };

    // If cascade type is null or frequency is 0, it's disabled
    if (result.type == input_shaper::Type::null || result.frequency <= 0.f) {
        return std::nullopt;
    }
    return result;
}

static std::optional<WeightAdjustConfig> get_weight_adjust_config(const WeightAdjustConfig &config, const M593Params &params) {
    if (params.weight_adjust.mass_limit && *params.weight_adjust.mass_limit == 0) {
        return std::nullopt;
    }
    return WeightAdjustConfig {
        .frequency_delta = params.weight_adjust.frequency_delta.value_or(config.frequency_delta),
        .mass_limit = params.weight_adjust.mass_limit.value_or(config.mass_limit),
    };
}

static void dump_axis_config(const AxisEnum axis, const AxisConfig &c, const std::optional<AxisConfig> &cascade = std::nullopt) {
    char buff[200];
    if (cascade) {
        snprintf(buff, sizeof(buff), "axis %c type=%s freq=%.1f damp=%.2f vr=%.1f | cascade: type=%s freq=%.1f damp=%.2f vr=%.1f",
            axis_codes[axis], to_string(c.type), c.frequency, c.damping_ratio, c.vibration_reduction,
            to_string(cascade->type), cascade->frequency, cascade->damping_ratio, cascade->vibration_reduction);
    } else {
        snprintf(buff, sizeof(buff), "axis %c type=%s freq=%.1f damp=%.2f vr=%.1f",
            axis_codes[axis], to_string(c.type), c.frequency, c.damping_ratio, c.vibration_reduction);
    }
    SERIAL_ECHO_START();
    SERIAL_ECHOLN(buff);
}

static void dump_weight_adjust_config(const char *prefix, const WeightAdjustConfig &c) {
    char buff[128];
    snprintf(buff, 128, "%s freq_delta=%f mass_limit=%f", prefix, c.frequency_delta, c.mass_limit);
    SERIAL_ECHO_START();
    SERIAL_ECHOLN(buff);
}

static void dump_current_config() {
    LOOP_XYZ(i) {
        if (const auto &axis_config = current_config().axis[i]) {
            dump_axis_config((AxisEnum)i, *axis_config, current_config().cascade[i]);
        } else {
            SERIAL_ECHO_START();
            SERIAL_ECHOLNPAIR("axis ", axis_codes[i], " disabled");
        }
    }
    if (const auto &weight_adjust_y = current_config().weight_adjust_y) {
        dump_weight_adjust_config("weight_adjust y", *weight_adjust_y);
    } else {
        SERIAL_ECHO_MSG("weight adjust y disabled");
    }
}

static M593Params clamp_frequency(M593Params params) {
    // Guard the optional dereference: a disengaged optional is never equal to
    // any value, so `opt != 0.` is true when unset and must not be dereferenced.
    if (params.axis.frequency && *params.axis.frequency != 0.f) {
        const float original_frequency = *params.axis.frequency;
        const float clamped_frequency = clamp_frequency_to_safe_values(original_frequency);
        if (clamped_frequency != original_frequency) {
            SERIAL_ECHO_MSG("Frequency clamped to safe values");
            params.axis.frequency = clamped_frequency;
        }
    }
    // Clamp the cascade frequency to the same safe range as the primary.
    // (A frequency of 0 means "disable cascade" and must be preserved.)
    if (params.cascade.frequency && *params.cascade.frequency != 0.f) {
        const float original_frequency = *params.cascade.frequency;
        const float clamped_frequency = clamp_frequency_to_safe_values(original_frequency);
        if (clamped_frequency != original_frequency) {
            SERIAL_ECHO_MSG("Cascade frequency clamped to safe values");
            params.cascade.frequency = clamped_frequency;
        }
    }
    return params;
}

static void M593_set_axis_config(const AxisEnum axis, const M593Params &params) {
    const AxisConfig &prev_config = current_config().axis[axis].value_or(axis_defaults[axis]);
    const std::optional<AxisConfig> next_config = get_axis_config(prev_config, clamp_frequency(params));
    set_axis_config(axis, next_config);
    set_config_for_m74(axis, next_config);

    // Also set cascade config if any cascade parameters seen
    if (params.cascade.type || params.cascade.frequency || params.cascade.damping_ratio || params.cascade.vibration_reduction) {
        const std::optional<AxisConfig> prev_cascade = current_config().cascade[axis];
        const std::optional<AxisConfig> next_cascade = get_cascade_config(prev_cascade, params);

        // Validate that the primary+cascade convolution fits the pulse buffer.
        // If it doesn't, the firmware will silently fall back to primary-only at
        // apply time, so warn here (where the user can act) rather than leaving
        // the configured-but-ignored situation undiagnosed.
        if (next_config && next_cascade && next_cascade->type != input_shaper::Type::null && next_cascade->frequency > 0.f) {
            const int primary_pulses = input_shaper::num_pulses_for_type(next_config->type);
            const int cascade_pulses = input_shaper::num_pulses_for_type(next_cascade->type);
            // On CoreXY two axes share the merge buffer, so the bound is the
            // per-axis slot count (INPUT_SHAPER_MAX_LENGTH); on non-CoreXY the
            // bound is the total (INPUT_SHAPER_MAX_PULSES == MAX_LENGTH here).
            const int max_pulses = INPUT_SHAPER_MAX_LENGTH;
            if (primary_pulses * cascade_pulses > max_pulses) {
                SERIAL_ECHO_MSG("?Cascade too large for buffer, falling back to primary-only");
                current_config().cascade[axis].reset();
                set_axis_config(axis, next_config);
                return;
            }
        }

        current_config().cascade[axis] = next_cascade;
        // Re-apply the axis config to update pulses (cascade changes the effective pulse train)
        set_axis_config(axis, next_config);
    }
}

static void M593_internal(const M593Params &params) {
    if (contains_axis_change(params)) {
        if (params.seen_x || (!params.seen_y && !params.seen_z)) {
            M593_set_axis_config(X_AXIS, params);
        }
        if (params.seen_y || (!params.seen_x && !params.seen_z)) {
            M593_set_axis_config(Y_AXIS, params);
        }
        if (params.seen_z || (!params.seen_x && !params.seen_y)) {
            M593_set_axis_config(Z_AXIS, params);
        }
    }
    if (contains_weight_adjust_change(params)) {
        const WeightAdjustConfig prev_config = current_config().weight_adjust_y.value_or(weight_adjust_y_default);
        const std::optional<WeightAdjustConfig> next_config = get_weight_adjust_config(prev_config, params);
        current_config().weight_adjust_y = next_config;
        set_config_for_m74(next_config);
    }
    if (params.seen_w) {
        config_store().set_input_shaper_config(current_config());
    }
    if (is_empty(params)) {
        dump_current_config();
    }
}

} // namespace input_shaper

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M593: Configure Input Shaping  <a href="https://reprap.org/wiki/G-code#M593:_Configure_Input_Shaping">M593: Configure Input Shaping</a>
 *
 *#### Usage
 *
 *    M593 [ D | F | T | R | X | Y | Z | A | M | W | U | C | B | Q ]
 *
 *#### Parameters
 *
 * - `D` - Set damping ratio. Range 0 to 1.
 * - `F` - Set frequency. Greater or equal to 0.
 *   - `0` - Default value is 0Hz = input shaper is disabled.
 * - `T` - Set type. Range 0 to 5
 *   - `0` - ZV Default
 *   - `1` - ZVD
 *   - `2` - MZV
 *   - `3` - EI
 *   - `4` - 2HUMP_EI
 *   - `5` - 3HUMP_EI
 * - `R` - Set vibration reduction. Greater than 0. Default value is 20.
 * - `X` - X axis
 * - `Y` - Y axis
 * - `Z` - Z axis
 * - `A` - Weight adjust frequency delta.
 * - `M` - Weight adjust mass limit.
 * - `W` - Write current input shaper settings to EEPROM.
 *
 * Cascade second shaper (two shapers in series per axis):
 * - `U` - Set cascade shaper type. Same range as T.
 * - `C` - Set cascade frequency. `0` disables cascade.
 * - `B` - Set cascade damping ratio. Range 0 to 1.
 * - `Q` - Set cascade vibration reduction. Greater than 0. Default 20.
 *
 * Without parameters prints the current Input Shaping settings
 */
void GcodeSuite::M593() {
    input_shaper::M593Params params;
    params.seen_x = parser.seen('X');
    params.seen_y = parser.seen('Y');
    params.seen_z = parser.seen('Z');
    params.seen_w = parser.seen('W');

    if (parser.seen('D')) {
        const float dr = parser.value_float();
        if (WITHIN(dr, 0., 1.)) {
            params.axis.damping_ratio = dr;
        } else {
            SERIAL_ECHO_MSG("?Damping ratio (D) value out of range (0-1)");
        }
    }

    if (parser.seen('F')) {
        const float f = parser.value_float();
        if (f >= 0) {
            params.axis.frequency = f;
        } else {
            SERIAL_ECHO_MSG("?Frequency (F) must be greater or equal to 0");
        }
    }

    if (parser.seen('T')) {
        const int t = parser.value_int();
        if (WITHIN(t, static_cast<int>(input_shaper::Type::first), static_cast<int>(input_shaper::Type::last))) {
            params.axis.type = static_cast<input_shaper::Type>(t);
        } else {
            SERIAL_ECHO_MSG("?Invalid type of input shaper (T)");
        }
    }

    if (parser.seen('R')) {
        const float vr = parser.value_float();
        if (vr > 0) {
            params.axis.vibration_reduction = vr;
        } else {
            SERIAL_ECHO_MSG("?Vibration reduction (R) must be greater than 0");
        }
    }

    // --- Cascade second shaper parameters ---
    // T2: cascade shaper type (0-5), F2: cascade frequency, D2: cascade damping, R2: cascade VR
    if (parser.seen('U')) {
        // Using 'U' for T2 since T2 is not a valid gcode param name
        const int t = parser.value_int();
        if (WITHIN(t, static_cast<int>(input_shaper::Type::first), static_cast<int>(input_shaper::Type::last))) {
            params.cascade.type = static_cast<input_shaper::Type>(t);
        } else {
            SERIAL_ECHO_MSG("?Invalid cascade type (U)");
        }
    }

    if (parser.seen('C')) {
        // Using 'C' for F2 (cascade frequency)
        const float f = parser.value_float();
        if (f >= 0) {
            params.cascade.frequency = f;
        } else {
            SERIAL_ECHO_MSG("?Cascade frequency (C) must be greater or equal to 0");
        }
    }

    if (parser.seen('B')) {
        // Using 'B' for D2 (cascade damping ratio)
        const float dr = parser.value_float();
        if (WITHIN(dr, 0., 1.)) {
            params.cascade.damping_ratio = dr;
        } else {
            SERIAL_ECHO_MSG("?Cascade damping ratio (B) value out of range (0-1)");
        }
    }

    if (parser.seen('Q')) {
        // Using 'Q' for R2 (cascade vibration reduction)
        const float vr = parser.value_float();
        if (vr > 0) {
            params.cascade.vibration_reduction = vr;
        } else {
            SERIAL_ECHO_MSG("?Cascade vibration reduction (Q) must be greater than 0");
        }
    }

    if (parser.seen('A')) {
        const float a = parser.value_float();
        if (a < 0) {
            params.weight_adjust.frequency_delta = a;
        } else {
            SERIAL_ECHO_MSG("?Weight adjust frequency delta (A) must be lesser than 0");
        }
    }

    if (parser.seen('M')) {
        const float m = parser.value_float();
        if (m >= 0) {
            params.weight_adjust.mass_limit = m;
        } else {
            SERIAL_ECHO_MSG("?Weight adjust mass limit (M) must be greater or equal to 0");
        }
    }

    input_shaper::M593_internal(params);
}

/** @}*/
