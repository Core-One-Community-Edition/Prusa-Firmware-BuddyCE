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

/// Data for the sweep_measuring phase.
/// Encodes progress and state for the automated frequency sweep.
struct sweep_measuring_data {
    uint8_t freq_start;   ///< Start frequency (Hz)
    uint8_t freq_end;     ///< End frequency (Hz)
    uint8_t freq_current; ///< Current frequency being measured (Hz)
    uint8_t flags;        ///< Bit 0: 1=calibrating accelerometer, 0=measuring; Bit 1: 1=motor B, 0=motor A

    sweep_measuring_data() = default;

    sweep_measuring_data(uint8_t start, uint8_t end, uint8_t current, bool calibrating, bool motor_b)
        : freq_start(start)
        , freq_end(end)
        , freq_current(current)
        , flags(static_cast<uint8_t>(calibrating ? 0x01 : 0x00) | static_cast<uint8_t>(motor_b ? 0x02 : 0x00)) {}

    bool is_calibrating() const { return flags & 0x01; }
    bool is_motor_b() const { return flags & 0x02; }
};

/// Data for the sweep_results phase.
struct sweep_results_data {
    uint8_t peak_freq_a; ///< Peak frequency for motor A (Hz), 0 if not measured
    uint8_t peak_freq_b; ///< Peak frequency for motor B (Hz), 0 if not measured
    uint8_t usb_status;  ///< 0=not attempted, 1=written, 2=no USB stick
    uint8_t shaped;     ///< 0=raw only, 1=raw+shaped files written

    sweep_results_data() = default;

    sweep_results_data(uint8_t peak_a, uint8_t peak_b, uint8_t usb, uint8_t shape = 0)
        : peak_freq_a(peak_a)
        , peak_freq_b(peak_b)
        , usb_status(usb)
        , shaped(shape) {}
};

enum class PhaseMotorVibration : PhaseUnderlyingType {
    /// Introduction to motor vibration tool
    intro,

    /// Choose sweep mode: Raw only or Raw + Validate (shaper applied)
    select_sweep_mode,

    /// Motor selection (A or B) — only for manual mode
    select_motor,

    /// Vibrate the selected motor, knob controls frequency (manual mode)
    vibrate,

    /// Auto home and park head at center (sweep mode)
    parking,

    /// Confirm accelerometer is attached (sweep mode)
    confirm_accelerometer,

    /// Automated frequency sweep with accelerometer (both motors)
    sweep_measuring,

    /// Show sweep results and USB dump status
    sweep_results,

    /// Tool finished
    finished,

    _cnt,
    _last = _cnt - 1
};

namespace ClientResponses {

inline constexpr EnumArray<PhaseMotorVibration, PhaseResponses, PhaseMotorVibration::_cnt> motor_vibration_responses {
    { PhaseMotorVibration::intro, { Response::Continue, Response::Abort } },
    { PhaseMotorVibration::select_sweep_mode, { Response::Left, Response::Right, Response::Abort } },
    { PhaseMotorVibration::select_motor, { Response::Left, Response::Right, Response::Abort } },
    { PhaseMotorVibration::vibrate, { Response::Done, Response::Abort } },
    { PhaseMotorVibration::parking, {} },
    { PhaseMotorVibration::confirm_accelerometer, { Response::Continue, Response::Abort } },
    { PhaseMotorVibration::sweep_measuring, { Response::Abort } },
    { PhaseMotorVibration::sweep_results, { Response::Continue, Response::Abort } },
    { PhaseMotorVibration::finished, { Response::Finish } },
};

} // namespace ClientResponses

constexpr inline ClientFSM client_fsm_from_phase(PhaseMotorVibration) { return ClientFSM::MotorVibration; }
