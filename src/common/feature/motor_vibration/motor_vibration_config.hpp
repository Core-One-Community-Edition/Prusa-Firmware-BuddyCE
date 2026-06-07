#pragma once

#include <printers.h>
#include <Marlin/src/feature/precise_stepping/common.hpp>

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

}; // namespace motor_vibration
