#include "motor_vibration_wizard.hpp"
#include "motor_vibration_config.hpp"

#include <Marlin/src/Marlin.h>
#include <Marlin/src/gcode/calibrate/M958.hpp>
#include <Marlin/src/gcode/gcode.h>
#include <Marlin/src/module/motion.h>
#include <mapi/motion.hpp>
#include <feature/motordriver_util.h>
#include <feature/phase_stepping/phase_stepping.hpp>

#include <fsm/motor_vibration_phases.hpp>
#include <common/marlin_server.hpp>
#include <bsod/bsod.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <time.h>

using namespace marlin_server;
using namespace motor_vibration;

namespace {

// ---------------------------------------------------------------------------
// Streaming sweep — writes per-frequency data to USB immediately,
// no large spectrum buffers on stack (avoids stack overflow on MCU).
// Only tracks peak frequency as we go.
// ---------------------------------------------------------------------------

/// Open a spectrum dump file on USB for a given motor label.
/// Returns the file handle, or nullptr if USB not available.
FILE *open_spectrum_file(char motor_label) {
    std::array<char, 1 + strlen("YYYYmmddHHMMSS")> strftime_buffer;
    const time_t curr_sec = time(nullptr);
    struct tm now;
    localtime_r(&curr_sec, &now);
    (void)strftime(strftime_buffer.data(), strftime_buffer.size(), "%Y%m%d%H%M%S", &now);

    std::array<char, 60> path;
    snprintf(path.data(), path.size(), "/usb/motor_sweep_%c_%s.txt", motor_label, strftime_buffer.data());

    FILE *f = fopen(path.data(), "w");
    if (f) {
        // Write TSV header
        constexpr const char *header = "frequency\tgain_x\tgain_y\tgain_z\tpsd\n";
        fwrite(header, strlen(header), 1, f);
    }
    return f;
}

/// Open a shaped spectrum dump file on USB for a given motor label.
/// Same format as raw, but with gains multiplied by shaper VR.
FILE *open_shaped_spectrum_file(char motor_label) {
    std::array<char, 1 + strlen("YYYYmmddHHMMSS")> strftime_buffer;
    const time_t curr_sec = time(nullptr);
    struct tm now;
    localtime_r(&curr_sec, &now);
    (void)strftime(strftime_buffer.data(), strftime_buffer.size(), "%Y%m%d%H%M%S", &now);

    std::array<char, 70> path;
    snprintf(path.data(), path.size(), "/usb/motor_sweep_%c_shaped_%s.txt", motor_label, strftime_buffer.data());

    FILE *f = fopen(path.data(), "w");
    if (f) {
        // Write TSV header with shaper info comment
        constexpr const char *header = "frequency\tgain_x\tgain_y\tgain_z\tpsd\n";
        fwrite(header, strlen(header), 1, f);
    }
    return f;
}

/// Write one row of spectrum data to the file.
void write_spectrum_row(FILE *f, float frequency, float gain_x, float gain_y, float gain_z) {
    if (!f) {
        return;
    }
    const float psd = sq(gain_x) + sq(gain_y) + sq(gain_z);
    std::array<char, 80> buffer;
    const int n = snprintf(buffer.data(), buffer.size(), "%.0f\t%.6f\t%.6f\t%.6f\t%.6f\n",
        static_cast<double>(frequency),
        static_cast<double>(gain_x),
        static_cast<double>(gain_y),
        static_cast<double>(gain_z),
        static_cast<double>(psd));
    fwrite(buffer.data(), n, 1, f);
}

/// Close and flush the spectrum file.
void close_spectrum_file(FILE *f) {
    if (f) {
        fflush(f);
        fclose(f);
    }
}

// ---------------------------------------------------------------------------
// Wizard implementation
// ---------------------------------------------------------------------------

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

        if (mode == WizardMode::sweep) {
            bool validate_mode = false;
            // Sweep mode selection: Raw only, or Raw + Validate (shaper applied)
            fsm_change(PhaseMotorVibration::select_sweep_mode);
            switch (wait_for_response(PhaseMotorVibration::select_sweep_mode)) {
            case Response::Left:
                validate_mode = false; // Raw only
                break;
            case Response::Right:
                validate_mode = true; // Raw + Validate
                break;
            case Response::Abort:
                return;
            default:
                bsod_unreachable();
                return;
            }
            run_sweep(validate_mode);
        } else {
            Motor selected_motor = select_motor();
            if (selected_motor == Motor::A || selected_motor == Motor::B) {
                run_manual_vibrate(selected_motor);
            }
        }

        // Finished
        fsm_change(PhaseMotorVibration::finished);
        wait_for_response(PhaseMotorVibration::finished);
    }

private:
    /// Select motor for manual vibration. Returns Motor::A or Motor::B, or
    /// an invalid value on abort.
    Motor select_motor() {
        fsm_change(PhaseMotorVibration::select_motor);
        switch (wait_for_response(PhaseMotorVibration::select_motor)) {
        case Response::Left:
            return Motor::A;
        case Response::Right:
            return Motor::B;
        case Response::Abort:
        default:
            return static_cast<Motor>(-1);
        }
    }

    /// Manual vibration mode: knob-controlled frequency, no accelerometer
    void run_manual_vibrate(Motor selected_motor) {
        MicrostepRestorer microstep_restorer;

        // Enable steppers and set microsteps
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
    }

    /// Automated sweep mode: home, park, confirm accelerometer, then measure both motors.
    /// If validate_mode is true, also computes shaped gains from current input shaper config.
    void run_sweep(bool validate_mode) {
        // Home and park — consistent measurement position
        parking();

        // Let user confirm accelerometer is attached
        if (!confirm_accelerometer()) {
            return; // aborted
        }

        // Read current input shaper config if validate mode
        std::optional<input_shaper::Shaper> shaper_x;
        std::optional<input_shaper::Shaper> shaper_y;
        if (validate_mode) {
            shaper_x = validate::get_current_shaper(X_AXIS);
            shaper_y = validate::get_current_shaper(Y_AXIS);
        }

        // Open USB files and sweep both motors
        uint8_t peak_freq_a = 0;
        uint8_t peak_freq_b = 0;
        uint8_t usb_status = 0; // 0=not attempted

        // Try opening USB file for motor A first — if it fails, no USB stick
        FILE *file_a = open_spectrum_file(sweep::motor_label(Motor::A));
        if (!file_a) {
            usb_status = 2; // no USB stick
        }

        FILE *file_a_shaped = nullptr;
        if (file_a && validate_mode) {
            file_a_shaped = open_shaped_spectrum_file(sweep::motor_label(Motor::A));
        }

        bool motor_a_ok = true;
        if (file_a) {
            motor_a_ok = sweep_motor(Motor::A, file_a, file_a_shaped, shaper_x, shaper_y, peak_freq_a);
            close_spectrum_file(file_a);
            close_spectrum_file(file_a_shaped);
        }

        bool motor_b_ok = true;
        if (motor_a_ok) {
            FILE *file_b = open_spectrum_file(sweep::motor_label(Motor::B));
            if (!file_b) {
                usb_status = 2;
            } else {
                FILE *file_b_shaped = nullptr;
                if (validate_mode) {
                    file_b_shaped = open_shaped_spectrum_file(sweep::motor_label(Motor::B));
                }
                motor_b_ok = sweep_motor(Motor::B, file_b, file_b_shaped, shaper_x, shaper_y, peak_freq_b);
                close_spectrum_file(file_b);
                close_spectrum_file(file_b_shaped);
            }
        }

        // Determine USB status
        if (usb_status == 0 && file_a) {
            usb_status = (motor_a_ok && motor_b_ok) ? 1 : 2;
        }

        // Show results
        const struct sweep_results_data results(peak_freq_a, peak_freq_b, usb_status, validate_mode ? 1 : 0);
        fsm_change(PhaseMotorVibration::sweep_results, fsm::serialize_data<sweep_results_data>(results));

        switch (wait_for_response(PhaseMotorVibration::sweep_results)) {
        case Response::Continue:
            break;
        case Response::Abort:
            break;
        default:
            break;
        }
    }

    /// Confirm accelerometer is attached. Returns false if aborted.
    bool confirm_accelerometer() {
        fsm_change(PhaseMotorVibration::confirm_accelerometer);
        switch (wait_for_response(PhaseMotorVibration::confirm_accelerometer)) {
        case Response::Continue:
            return true;
        case Response::Abort:
            return false;
        default:
            bsod_unreachable();
            return false;
        }
    }

    /// Home the printer and park the head at center for consistent measurement.
    void parking() {
        fsm_change(PhaseMotorVibration::parking);

        // Home if not homed
        GcodeSuite::G28_no_parser(true, true, true,
            {
                .only_if_needed = true,
                .precise = false,
            });

        mapi::ensure_tool_with_accelerometer_picked();

        // Park at center for consistent accelerometer measurements
        do_blocking_move_to(X_BED_SIZE / 2, Y_BED_SIZE / 2, Z_SIZE / 2);
    }

    /// Sweep a single motor across the frequency range.
    /// Streams data to the open file handle per-frequency (no buffering).
    /// Tracks peak frequency in peak_freq_out.
    /// If file_shaped is non-null, also computes shaped gains using shaper_x/y.
    /// Returns false if aborted.
    bool sweep_motor(Motor motor, FILE *file, FILE *file_shaped,
        const std::optional<input_shaper::Shaper> &shaper_x,
        const std::optional<input_shaper::Shaper> &shaper_y,
        uint8_t &peak_freq_out) {
        const AxisEnum logicalAxis = sweep::logical_axis(motor);
        const StepEventFlag_t motor_flag = motor_axis_flag(motor);
        const bool is_motor_b = (motor == Motor::B);

        // Initial phase data
        struct sweep_measuring_data data(
            sweep::frequency_range.start,
            sweep::frequency_range.end,
            sweep::frequency_range.start,
            true, // calibrating
            is_motor_b);
        marlin_server::fsm_change(PhaseMotorVibration::sweep_measuring, fsm::serialize_data<sweep_measuring_data>(data));

        // Disable phase stepping and set up microsteps
        // Note: phase_stepping::EnsureDisabled is already held by the wizard
        MicrostepRestorer microstep_restorer;
        enable_all_steppers();
        stepper_microsteps(X_AXIS, 128);
        stepper_microsteps(Y_AXIS, 128);

        VibrateMeasureParams args {
            .excitation_acceleration = sweep::acceleration,
            .excitation_cycles = sweep::cycles,
            .klipper_mode = true,
            .calibrate_accelerometer = true,
            .axis_flag = motor_flag,
        };

        if (!args.setup(microstep_restorer)) {
            return false;
        }

        // Progress hook: check for abort, update FSM
        struct ProgressState {
            bool aborted = false;
            float prev_progress = -1.0f;
        } progress_state;

        const auto progress_hook = [&progress_state, is_motor_b](const VibrateMeasureProgressHookParams &params) {
            // Check for abort
            switch (marlin_server::get_response_from_phase(PhaseMotorVibration::sweep_measuring)) {
            case Response::Abort:
                progress_state.aborted = true;
                return false;
            case Response::_none:
                break;
            default:
                break;
            }

            // Report calibration progress
            if (params.phase == VibrateMeasureProgressHookParams::Phase::calibrating
                && abs(params.progress - progress_state.prev_progress) >= 0.01f) {
                struct sweep_measuring_data cal_data(
                    sweep::frequency_range.start,
                    sweep::frequency_range.end,
                    static_cast<uint8_t>(255 * params.progress),
                    true, // calibrating
                    is_motor_b);
                marlin_server::fsm_change(PhaseMotorVibration::sweep_measuring,
                    fsm::serialize_data<sweep_measuring_data>(cal_data));
                progress_state.prev_progress = params.progress;
            }

            idle(true);
            return true;
        };

        // Frequency sweep loop — stream to file, track peak only
        float frequency = sweep::frequency_range.start;
        float max_psd = 0;
        uint8_t peak_freq = 0;
        bool any_valid = false;

        for (size_t i = 0; i < sweep::sample_count; ++i) {
            // Check for abort
            if (progress_state.aborted) {
                return false;
            }

            // Update progress
            struct sweep_measuring_data measure_data(
                sweep::frequency_range.start,
                sweep::frequency_range.end,
                static_cast<uint8_t>(frequency),
                args.calibrate_accelerometer,
                is_motor_b);
            marlin_server::fsm_change(PhaseMotorVibration::sweep_measuring,
                fsm::serialize_data<sweep_measuring_data>(measure_data));

            auto result = vibrate_measure_repeat(args, frequency, progress_hook);
            args.calibrate_accelerometer = false;

            if (!result.has_value()) {
                // Measurement failed — write zero row
                write_spectrum_row(file, frequency, 0, 0, 0);
                if (file_shaped) {
                    write_spectrum_row(file_shaped, frequency, 0, 0, 0);
                }
            } else {
                // Subtract excitation from the driven axis gain
                result->gain[logicalAxis] = std::max(result->gain[logicalAxis] - 1.f, 0.f);

                // Write per-axis gains immediately
                write_spectrum_row(file, frequency,
                    result->gain[X_AXIS],
                    result->gain[Y_AXIS],
                    result->gain[Z_AXIS]);

                // Compute and write shaped gains if validate mode
                if (file_shaped) {
                    float shaped_x = result->gain[X_AXIS];
                    float shaped_y = result->gain[Y_AXIS];
                    float shaped_z = result->gain[Z_AXIS];

                    // Apply each axis's shaper vibration reduction factor
                    if (shaper_x.has_value()) {
                        shaped_x *= static_cast<float>(validate::vibration_reduction_factor(
                            *shaper_x, 0.1f, frequency));
                    }
                    if (shaper_y.has_value()) {
                        shaped_y *= static_cast<float>(validate::vibration_reduction_factor(
                            *shaper_y, 0.1f, frequency));
                    }

                    write_spectrum_row(file_shaped, frequency, shaped_x, shaped_y, shaped_z);
                }

                // Track peak
                const float psd = result->gain_square();
                if (psd > max_psd) {
                    max_psd = psd;
                    peak_freq = static_cast<uint8_t>(frequency);
                }
                if (psd > 0.001f) {
                    any_valid = true;
                }
            }

            frequency += sweep::frequency_range.increment;
        }

        if (any_valid) {
            peak_freq_out = peak_freq;
        }

        return true;
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
