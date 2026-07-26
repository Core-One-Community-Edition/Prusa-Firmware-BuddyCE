#include "PrusaGcodeSuite.hpp"
#include <feature/motor_vibration/motor_vibration_wizard.hpp>
#include <common/marlin_server.hpp>

/**
 *### M962: Motor Vibration Tool
 *
 * Vibrates a selected motor at adjustable frequency to find resonating parts.
 */
void PrusaGcodeSuite::M962() {
    // Refuse while a print is active: the wizard homes, disables phase stepping
    // and drives motors, which would corrupt an ongoing print. The menu item is
    // intentionally not hidden during printing (matching the M961 sibling), so
    // guard here instead.
    if (marlin_server::is_printing()) {
        SERIAL_ECHO_MSG("?Cannot run motor vibration while printing");
        return;
    }
    motor_vibration::run_wizard(motor_vibration::WizardMode::manual);
}
