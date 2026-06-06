#pragma once

#include <cstdint>
#include <optional>
#include <temperature.hpp>
#include <cmath>

#include <pwm_utils.hpp>

namespace buddy {

/// Algorithm for controlling the cooling on the xbuddy extension.
class FanCooling {
public:
    using FanPWM = PWM255;
    using FanPWMOrAuto = PWM255OrAuto;

#if PRINTER_IS_PRUSA_COREONE()
    /// Temperature at which the fans start spinning at full speed, no matter what
    static constexpr Temperature overheating_temp = 70.0f;

    /// Temperature at which the print is stopped and fans at full power, not matter what
    static constexpr Temperature critical_temp = 75.0f;

    /// Temperature at which the normal fan control may be restored, after emergency temperature
    static constexpr Temperature recovery_temp = 65.0f;
#elif PRINTER_IS_PRUSA_COREONEL()
    /// Temperature at which the fans start spinning at full speed, no matter what
    static constexpr Temperature overheating_temp = 65.0f;

    /// Temperature at which the print is stopped and fans at full power, not matter what
    static constexpr Temperature critical_temp = 68.0f;

    /// Temperature at which the normal fan control may be restored, after emergency temperature
    static constexpr Temperature recovery_temp = 60.0f;
#else
    #error
#endif

    // Numbers pulled out of thin air
    static constexpr FanPWM max_pwm { 255 };
    static constexpr FanPWM min_pwm { 40 }; // TODO - This is too much, we need to find a lower value that is still safe
    static constexpr FanPWM spin_up_pwm { 100 };

    // time step for regulation loop
    static constexpr float dt_s = 1.0f;

    static constexpr float integration_constant = 1.5f * dt_s; // used for legacy regulator only

    /// Applies spinup and emergency fan overrides
    [[nodiscard]] FanPWM apply_pwm_overrides(bool already_spinning, FanPWM pwm) const;

    // Compute at what PWM the fan(s) should be driven
    // !!!!!!!! this function should be called in regular time intervals given by dt_s !!!!!!!!
    [[nodiscard]] FanPWM compute_pwm_step(Temperature current_temperature, std::optional<Temperature> target_temperature, FanPWMOrAuto target_pwm, FanPWM max_auto_pwm, std::optional<Temperature> heatbreak_temperature);

    constexpr bool get_overheating_temp_flag() { return overheating_temp_flag; };
    constexpr bool get_critical_temp_flag() { return critical_temp_flag; };

    constexpr FanPWM get_heatbreak_pwm_boost() const { return FanPWM { static_cast<FanPWM::Value>(heatbreak_pwm_boost) }; };

    uint8_t ramp_breakpoint_pwm = 0;
    float ramp_slope = 10.0f;
    bool regulator_legacy = true; // Legacy regulator for old gcode compatibility

    /// Maximum allowed heatbreak temperature, set via M9160. nullopt = heatbreak limiter disabled.
    std::optional<Temperature> heatbreak_max_temp;

private:
    /// Computes a PWM ramping function
    FanPWM compute_auto_regulation_step(Temperature current_temperature, Temperature target_temperature, FanPWM max_auto_pwm);
    FanPWM compute_auto_regulation_step_legacy(Temperature current_temperature, Temperature target_temperature, FanPWM max_auto_pwm);

    /// Integrates the heatbreak limiter boost, see compute_pwm_step
    FanPWM compute_heatbreak_boost_step(std::optional<Temperature> heatbreak_temperature);

    // integration constant for the heatbreak limiter boost, in PWM per (°C * dt_s)
    static constexpr float heatbreak_boost_integration_constant = 0.5f * dt_s;

    float last_regulation_output = 0.0f;
    float heatbreak_pwm_boost = 0.0f;

    bool overheating_temp_flag = false;
    bool critical_temp_flag = false;
};

} // namespace buddy
