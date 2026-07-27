#include "screen_menu_advanced_calibration.hpp"

#if HAS_BED_LEVEL_PROBE() || HAS_MOTOR_VIBRATION()

ScreenMenuAdvancedCalibration::ScreenMenuAdvancedCalibration()
    : ScreenMenuAdvancedCalibration__(_(label)) {}

#endif // HAS_BED_LEVEL_PROBE() || HAS_MOTOR_VIBRATION()
