#pragma once

#include <printers.h>
#include <Marlin/src/feature/precise_stepping/common.hpp>
#include <Marlin/src/feature/input_shaper/input_shaper.hpp>
#include <Marlin/src/feature/input_shaper/input_shaper_config.hpp>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace motor_vibration {

/// Motor selection for vibration
enum class Motor {
    A, ///< Motor A — vibrates along X-axis on CoreXY
    B, ///< Motor B — vibrates along Y-axis on CoreXY
};

// frequency range — wider than belt tuning because we're hunting
// for resonances in arbitrary parts, not just belts
constexpr uint16_t freq_min = 20;
constexpr uint16_t freq_max = 200;

// acceleration scales with frequency to keep vibration amplitude reasonable
constexpr uint16_t accel_min = 5000;
constexpr uint16_t accel_max = 12000;

/// Get the step event flags for the given motor selection.
/// Motor A (X-axis motion on CoreXY): both steppers step in same direction
/// Motor B (Y-axis motion on CoreXY): Y stepper direction reversed
constexpr StepEventFlag_t motor_axis_flag(Motor motor) {
    switch (motor) {
    case Motor::A:
        return STEP_EVENT_FLAG_STEP_X | STEP_EVENT_FLAG_STEP_Y;
    case Motor::B:
        return STEP_EVENT_FLAG_STEP_X | STEP_EVENT_FLAG_STEP_Y | STEP_EVENT_FLAG_Y_DIR;
    }
    return 0; // unreachable
}

// calculate acceleration for desired frequency
constexpr float calc_accel(float freq) {
    return accel_min + (freq - freq_min) * (accel_max - accel_min) / (freq_max - freq_min);
}

// ---------------------------------------------------------------------------
// Sweep mode: automated frequency sweep with accelerometer measurement
// ---------------------------------------------------------------------------
namespace sweep {

/// Frequency range for the motor sweep
struct FrequencyRange {
    int start;
    int end;
    int increment;
};

constexpr FrequencyRange frequency_range {
    .start = freq_min,
    .end = freq_max,
    .increment = 1,
};

/// Number of frequency samples in the sweep
constexpr size_t sample_count = (frequency_range.end - frequency_range.start) / frequency_range.increment;

/// Excitation acceleration for the sweep (m/s²).
/// Higher than manual mode to get measurable accelerometer readings.
/// Same value as M958/M1959 uses (2.5 m/s²).
constexpr float acceleration = 2.5f;

/// Excitation cycles per frequency measurement point
constexpr uint32_t cycles = 50;

/// Logical axis corresponding to each motor on CoreXY.
/// Motor A drives X-axis motion, Motor B drives Y-axis motion.
constexpr AxisEnum logical_axis(Motor motor) {
    switch (motor) {
    case Motor::A: return X_AXIS;
    case Motor::B: return Y_AXIS;
    }
    return NO_AXIS_ENUM;
}

/// Character label for file naming
constexpr char motor_label(Motor motor) {
    switch (motor) {
    case Motor::A: return 'A';
    case Motor::B: return 'B';
    }
    return '?';
}

} // namespace sweep

// ---------------------------------------------------------------------------
// Shaper validation: compute vibration reduction factor for currently
// active input shaper configuration.
// ---------------------------------------------------------------------------
namespace validate {

/// Compute vibration reduction factor for a shaper at a given frequency.
/// Returns a value in [0, 1] where 0 = complete suppression, 1 = no effect.
/// This is a port of the same-named function in M958.cpp, made accessible
/// for the motor vibration wizard.
inline double vibration_reduction_factor(
    const input_shaper::Shaper &shaper,
    float system_damping_ratio,
    float frequency) {

    // inv_D = 1 / sum(shaper pulse amplitudes)
    double d = 0.;
    for (int i = 0; i < shaper.num_pulses; ++i) {
        d += static_cast<double>(shaper.a[i]);
    }
    const double inv_D = 1. / d;

    const double omega = 2. * std::numbers::pi_v<double> * static_cast<double>(frequency);
    const double damping = static_cast<double>(system_damping_ratio) * omega;
    const double omega_d = omega * std::sqrt(1. - static_cast<double>(sq(system_damping_ratio)));

    double s = 0.;
    double c = 0.;

    for (int i = 0; i < shaper.num_pulses; ++i) {
        const double a = static_cast<double>(shaper.a[i]);
        const double t = static_cast<double>(shaper.t[i]);
        const double t_last = static_cast<double>(shaper.t[shaper.num_pulses - 1]);
        const double w = a * std::exp(-damping * (t_last - t));
        s += w * std::sin(omega_d * t);
        c += w * std::cos(omega_d * t);
    }
    return std::sqrt(sq(s) + sq(c)) * inv_D;
}

/// Read the currently active shaper for a logical axis.
/// Returns the Shaper struct, or std::nullopt if no shaper is configured (null type).
inline std::optional<input_shaper::Shaper> get_current_shaper(AxisEnum axis) {
    const auto config = input_shaper::get_axis_config(axis);
    if (!config.has_value() || config->type == input_shaper::Type::null) {
        return std::nullopt;
    }
    return input_shaper::get(
        config->damping_ratio,
        config->frequency,
        config->vibration_reduction,
        config->type);
}

} // namespace validate

}; // namespace motor_vibration
