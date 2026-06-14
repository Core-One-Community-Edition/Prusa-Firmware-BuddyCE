#pragma once

#include <screen_fsm.hpp>
#include <radio_button_fsm.hpp>
#include <option/has_bed_level_probe.h>

#if HAS_BED_LEVEL_PROBE()

class ScreenBedLevelProbe final : public ScreenFSM {
public:
    ScreenBedLevelProbe();
    ~ScreenBedLevelProbe();

    inline PhaseBedLevelProbe get_phase() const {
        return GetEnumFromPhaseIndex<PhaseBedLevelProbe>(fsm_base_data.GetPhase());
    }

protected:
    void create_frame() final;
    void destroy_frame() final;
    void update_frame() final;
};

#endif // HAS_BED_LEVEL_PROBE()
