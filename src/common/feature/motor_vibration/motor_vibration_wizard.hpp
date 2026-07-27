#pragma once

namespace motor_vibration {

enum class WizardMode {
    manual,
    sweep,
};

void run_wizard(WizardMode mode);

} // namespace motor_vibration
