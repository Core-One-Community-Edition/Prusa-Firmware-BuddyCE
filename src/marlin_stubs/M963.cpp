#include "PrusaGcodeSuite.hpp"
#include <feature/motor_vibration/motor_vibration_wizard.hpp>

/**
 *### M963: Motor Sweep
 *
 * Automated frequency sweep with accelerometer to measure motor
 * resonances and save spectra to USB.
 */
void PrusaGcodeSuite::M963() {
    motor_vibration::run_wizard(motor_vibration::WizardMode::sweep);
}
