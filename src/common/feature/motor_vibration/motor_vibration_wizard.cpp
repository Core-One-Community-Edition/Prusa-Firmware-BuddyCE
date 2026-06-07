#include "motor_vibration_wizard.hpp"
#include "motor_vibration_config.hpp"

#include <Marlin/src/Marlin.h>
#include <Marlin/src/gcode/calibrate/M958.hpp>
#include <feature/motordriver_util.h>
#include <feature/phase_stepping/phase_stepping.hpp>

#include <fsm/motor_vibration_phases.hpp>
#include <common/marlin_server.hpp>
#include <bsod/bsod.h>

#include <cmath>

using namespace marlin_server;
using namespace motor_vibration;

namespace {

class MotorVibrationWizard {
private:
    phase_stepping::EnsureDisabled phase_stepping_disabler;

public:
    MotorVibrationWizard() {}
    ~MotorVibrationWizard() {
        disable_all_steppers();
    }

    void run(WizardMode mode) {
        planner.finish_and_disable();
        FSM_Holder holder { PhaseMotorVibration::intro };

        if (!wait_for_continue(PhaseMotorVibration::intro)) {
            return;
        }

        // Motor selection
        fsm_change(PhaseMotorVibration::select_motor);
        Motor selected_motor = Motor::A;
        switch (wait_for_response(PhaseMotorVibration::select_motor)) {
        case Response::Left:
            selected_motor = Motor::A;
            break;
        case Response::Right:
            selected_motor = Motor::B;
            break;
        case Response::Abort:
            return;
        default:
            bsod_unreachable();
            return;
        }

        MicrostepRestorer microstep_restorer;

        // Enable steppers and set microsteps (same as belt tuning)
        // MicrostepRestorer must be constructed BEFORE changing microsteps,
        // so it captures the original mres for correct step_len calculation
        enable_all_steppers();
        stepper_microsteps(X_AXIS, 128);
        stepper_microsteps(Y_AXIS, 128);

        Vibrate vibrator {
            .frequency = 80,
            .excitation_acceleration = abs(calc_accel(80) * 0.001f),
            .axis_flag = motor_axis_flag(selected_motor),
        };

        if (!vibrator.setup(microstep_restorer)) {
            return;
        }

        // Vibrate with knob-controlled frequency
        const struct vibration_data data(vibrator.frequency);
        fsm_change(PhaseMotorVibration::vibrate, fsm::serialize_data<vibration_data>(data));
        resonate(vibrator, PhaseMotorVibration::vibrate);

        // Finished
        fsm_change(PhaseMotorVibration::finished);
        wait_for_response(PhaseMotorVibration::finished);
    }

    void adjust_vibrator(Vibrate &vibrator) {
        vibrator.frequency = std::clamp<float>(abs(vibrator.frequency), freq_min, freq_max);
        vibrator.excitation_acceleration = abs(calc_accel(vibrator.frequency) * 0.001f);
    }

    Response resonate(Vibrate &vibrator, PhaseMotorVibration phase) {
        Response response = Response::_none;
        auto knob_pos = get_knob_position();
        while (true) {
            {
                const auto new_knob_pos = get_knob_position();
                vibrator.frequency += (new_knob_pos - knob_pos) * 0.5f;
                knob_pos = new_knob_pos;
            }

            adjust_vibrator(vibrator);

            const struct vibration_data data(vibrator.frequency);
            fsm_change(phase, fsm::serialize_data<vibration_data>(data));

            vibrator.step();

            if ((response = get_response_from_phase(phase)) != Response::_none) {
                break;
            }

            idle(true);
        }
        return response;
    }

    bool wait_for_continue(PhaseMotorVibration phase) const {
        switch (wait_for_response(phase)) {
        case Response::Continue:
            return true;
        case Response::Abort:
            break;
        default:
            bsod_unreachable();
            break;
        }
        return false;
    }
};

} // namespace

void motor_vibration::run_wizard(WizardMode mode) {
    MotorVibrationWizard wizard;
    wizard.run(mode);
}
