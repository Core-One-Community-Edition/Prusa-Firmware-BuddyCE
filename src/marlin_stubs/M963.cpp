#include "PrusaGcodeSuite.hpp"
#include <feature/motor_vibration/motor_vibration_wizard.hpp>
#include <common/marlin_server.hpp>

/**
 *### M963: Motor Sweep
 *
 * Automated frequency sweep with accelerometer to measure motor
 * resonances and save spectra to USB.
 */
void PrusaGcodeSuite::M963() {
    // Refuse while a print is active: the sweep homes, disables phase stepping
    // and drives motors for ~10 minutes, which would corrupt an ongoing print.
    if (marlin_server::is_printing()) {
        SERIAL_ECHO_MSG("?Cannot run motor sweep while printing");
        return;
    }
    motor_vibration::run_wizard(motor_vibration::WizardMode::sweep);
}
