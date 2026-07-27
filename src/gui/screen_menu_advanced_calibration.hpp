#pragma once

#include <option/has_bed_level_probe.h>
#include <option/has_motor_vibration.h>

#if HAS_BED_LEVEL_PROBE() || HAS_MOTOR_VIBRATION()

#include <screen_menu.hpp>
#include "MItem_tools.hpp"

using ScreenMenuAdvancedCalibration__ = ScreenMenu<EFooter::On, MI_RETURN
#if HAS_BED_LEVEL_PROBE()
    , MI_SHIM_CALIBRATION
    , MI_BED_LEVEL_PROBE
#endif
#if HAS_MOTOR_VIBRATION()
    , MI_MOTOR_VIBRATION_MANUAL
    , MI_MOTOR_VIBRATION_SWEEP
#endif
    >;

class ScreenMenuAdvancedCalibration : public ScreenMenuAdvancedCalibration__ {
    static constexpr const char *label = N_("ADVANCED CALIBRATION");

public:
    ScreenMenuAdvancedCalibration();
};

#endif // HAS_BED_LEVEL_PROBE() || HAS_MOTOR_VIBRATION()
