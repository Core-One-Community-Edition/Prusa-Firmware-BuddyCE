#include "screen_menu_advanced_calibration.hpp"

#if HAS_BED_LEVEL_PROBE()

ScreenMenuAdvancedCalibration::ScreenMenuAdvancedCalibration()
    : ScreenMenuAdvancedCalibration__(_(label)) {}

#endif // HAS_BED_LEVEL_PROBE()
