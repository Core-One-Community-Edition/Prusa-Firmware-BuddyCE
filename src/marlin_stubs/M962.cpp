#include "PrusaGcodeSuite.hpp"
#include <feature/motor_vibration/motor_vibration_wizard.hpp>

/**
 *### M962: Motor Vibration Tool
 *
 * Vibrates a selected motor at adjustable frequency to find resonating parts.
 */
void PrusaGcodeSuite::M962() {
    motor_vibration::run_wizard(motor_vibration::WizardMode::manual);
}
