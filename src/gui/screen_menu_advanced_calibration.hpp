#pragma once

#include <option/has_bed_level_probe.h>

#if HAS_BED_LEVEL_PROBE()

#include <screen_menu.hpp>
#include "MItem_tools.hpp"

using ScreenMenuAdvancedCalibration__ = ScreenMenu<EFooter::On, MI_RETURN, MI_SHIM_CALIBRATION, MI_BED_LEVEL_PROBE>;

class ScreenMenuAdvancedCalibration : public ScreenMenuAdvancedCalibration__ {
    static constexpr const char *label = N_("ADVANCED CALIBRATION");

public:
    ScreenMenuAdvancedCalibration();
};

#endif // HAS_BED_LEVEL_PROBE()
