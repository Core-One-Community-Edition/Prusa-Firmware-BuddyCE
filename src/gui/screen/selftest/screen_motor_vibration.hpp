#pragma once

#include <screen_fsm.hpp>
#include <marlin_server_types/fsm/motor_vibration_phases.hpp>

class ScreenMotorVibration final : public ScreenFSM {

public:
    ScreenMotorVibration();
    ~ScreenMotorVibration();

protected:
    inline PhaseMotorVibration get_phase() const {
        return GetEnumFromPhaseIndex<PhaseMotorVibration>(fsm_base_data.GetPhase());
    }

protected:
    void create_frame() override;
    void destroy_frame() override;
    void update_frame() override;
};
