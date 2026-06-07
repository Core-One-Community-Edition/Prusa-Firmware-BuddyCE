/// \file
#pragma once

#include <marlin_server_types/client_response.hpp>

/// Frequency has 0.5f step and we need to send this through 4B
struct vibration_data {
    uint16_t frequency_x2 = 0; /// Frequency is passed multiplied by 2

    vibration_data() = default;

    vibration_data(float freq) {
        set(freq);
    }

    void set(float freq) { frequency_x2 = static_cast<uint16_t>(freq * 2); }
    float get() const { return static_cast<float>(frequency_x2) / 2.0f; }
};

enum class PhaseMotorVibration : PhaseUnderlyingType {
    /// Introduction to motor vibration tool
    intro,

    /// Motor selection (A or B)
    select_motor,

    /// Vibrate the selected motor, knob controls frequency
    vibrate,

    /// Tool finished
    finished,

    _cnt,
    _last = _cnt - 1
};

namespace ClientResponses {

inline constexpr EnumArray<PhaseMotorVibration, PhaseResponses, PhaseMotorVibration::_cnt> motor_vibration_responses {
    { PhaseMotorVibration::intro, { Response::Continue, Response::Abort } },
    { PhaseMotorVibration::select_motor, { Response::Left, Response::Right, Response::Abort } },
    { PhaseMotorVibration::vibrate, { Response::Done, Response::Abort } },
    { PhaseMotorVibration::finished, { Response::Finish } },
};

} // namespace ClientResponses

constexpr inline ClientFSM client_fsm_from_phase(PhaseMotorVibration) { return ClientFSM::MotorVibration; }
