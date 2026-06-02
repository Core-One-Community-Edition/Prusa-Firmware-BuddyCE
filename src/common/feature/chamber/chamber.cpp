#include "chamber.hpp"

#include <buddy/unreachable.hpp>
#include <cmath>
#include <config_store/store_instance.hpp>
#include <marlin_server.hpp>
#include <marlin_server_shared.h>
#include <option/has_chamber_vents.h>
#include <option/has_xbuddy_extension.h>
#include <feature/safety_timer/safety_timer.hpp>
#include "chamber_enums.hpp"

#if HAS_CHAMBER_VENTS()
    #include <marlin_stubs/feature/automatic_chamber_vents/automatic_chamber_vents.hpp>
#endif

#if XL_ENCLOSURE_SUPPORT()
    #include <hw/xl/xl_enclosure.hpp>
#endif

#if HAS_XBUDDY_EXTENSION()
    #include <feature/xbuddy_extension/xbuddy_extension.hpp>
#endif

#if PRINTER_IS_PRUSA_COREONE() || PRINTER_IS_PRUSA_COREONEL()
    #define HAS_CHAMBER_TEMPERATURE_THERMISTOR_POSITION_OFFSET() 1
#elif PRINTER_IS_PRUSA_XL()
    #define HAS_CHAMBER_TEMPERATURE_THERMISTOR_POSITION_OFFSET() 0
#else
    #error
#endif

#if HAS_CHAMBER_TEMPERATURE_THERMISTOR_POSITION_OFFSET()
    #include <Configuration.h>
#endif

#if PRINTER_IS_PRUSA_COREONE()
    #include <timing.h>
    #include <Marlin/src/module/temperature.h>
#endif

#if PRINTER_IS_PRUSA_COREONE()
namespace {
constexpr buddy::Temperature chamber_maxtemp = 65;
constexpr buddy::Temperature chamber_maxtemp_safety_margin = 5;
} // namespace
#elif PRINTER_IS_PRUSA_COREONEL()
namespace {
constexpr buddy::Temperature chamber_maxtemp = 65;
constexpr buddy::Temperature chamber_maxtemp_safety_margin = 5;
} // namespace
#endif

namespace buddy {

Chamber &chamber() {
    static Chamber instance;
    return instance;
}

void Chamber::step() {
    assert(osThreadGetId() == marlin_server::server_task);

    std::lock_guard _lg(mutex_);

#if XL_ENCLOSURE_SUPPORT()
    thermistor_temperature_ = xl_enclosure.getEnclosureTemperature();

#elif HAS_XBUDDY_EXTENSION()
    thermistor_temperature_ = xbuddy_extension().chamber_temperature();
#endif

#if PRINTER_IS_PRUSA_COREONE()
    // --- Heatbreak proxy for chamber temperature ---
    // The heatbreak thermistor tracks chamber air temperature (plus a constant hotend conduction offset)
    // and is NOT affected by chamber cooling fan airflow. This eliminates the oscillation feedback loop
    // caused by the XBE thermistor being cooled by the very fans it controls.
    //
    // Safety margin analysis (heatbreak proxy, offset=10°C):
    //   overheating (70°C reported) → loveboard ~60°C → 5°C margin ✓
    //   critical    (75°C reported) → loveboard ~65°C → 0°C margin (acceptable: print kill + heaters off)
    constexpr float heatbreak_chamber_offset = 10.0f; // empirical: hotend conduction offset (needs calibration)
    constexpr float heatbreak_switch_point = 36.0f; // heatbreak PID target (DEFAULT_HEATBREAK_TEMPERATURE)
    constexpr float heatbreak_switch_hysteresis = 5.0f; // latch activation threshold: switch_point + hysteresis = 41°C
    constexpr float heatbreak_ema_tau = 30.0f; // EMA time constant (seconds) — filters PID cycling & part cooling transients
    constexpr float heatbreak_min_valid = 5.0f; // below this, heatbreak reading is invalid (HEATBREAK_MINTEMP)

    const float raw_heatbreak = thermalManager.degHeatbreak(0);

    if (raw_heatbreak >= heatbreak_min_valid) {
        const uint32_t now_ms = ticks_ms();

        if (heatbreak_ema_ < 0.0f) {
            // First valid reading — initialize EMA directly (follows init_bed_frame_est_celsius pattern)
            heatbreak_ema_ = raw_heatbreak;
        } else {
            const float dt_s = ticks_diff(now_ms, last_heatbreak_ema_ms_) / 1000.0f;
            if (dt_s > 0.0f) {
                const float alpha = 1.0f - expf(-dt_s / heatbreak_ema_tau);
                heatbreak_ema_ += alpha * (raw_heatbreak - heatbreak_ema_);
            }
        }
        last_heatbreak_ema_ms_ = now_ms;

        // One-way latch: once heatbreak sensor activates, it stays active until reset()
        // This prevents sensor-source toggling during warm-up/cool-down transitions
        if (!using_heatbreak_sensor_ && heatbreak_ema_ > heatbreak_switch_point + heatbreak_switch_hysteresis) {
            using_heatbreak_sensor_ = true;
        }

        if (using_heatbreak_sensor_) {
            thermistor_temperature_ = heatbreak_ema_ - heatbreak_chamber_offset;
        }
    }
    // else: heatbreak sensor invalid (disconnected or pre-ISR) → keep XBE value, degrade gracefully
#endif

    METRIC_DEF(metric_chamber_temp, "chamber_temp", METRIC_VALUE_FLOAT, 1000, METRIC_ENABLED);
    if (thermistor_temperature_.has_value()) {
        metric_record_float(&metric_chamber_temp, thermistor_temperature_.value());
    } else {
        metric_record_float(&metric_chamber_temp, NAN);
    }
}

Chamber::Capabilities Chamber::capabilities_nolock() const {
    switch (backend()) {

#if XL_ENCLOSURE_SUPPORT()
    case Backend::xl_enclosure:
        return Capabilities {
            .temperature_reporting = true,
        };
#endif

#if HAS_XBUDDY_EXTENSION()
    case Backend::xbuddy_extension:
        return Capabilities {
            .temperature_reporting = true,
            .cooling = xbuddy_extension().can_auto_cool(),
            // Always show temperature control menu items, even if auto cooling is disabled
                .always_show_temperature_control = true,

    #if PRINTER_IS_PRUSA_COREONE() || PRINTER_IS_PRUSA_COREONEL()
            .max_temp = { chamber_maxtemp - chamber_maxtemp_safety_margin },
    #endif
        };
#endif

    case Backend::none:
        return Capabilities {};
    }
    BUDDY_UNREACHABLE();
}

Chamber::Capabilities Chamber::capabilities() const {
    std::lock_guard _lg(mutex_);

    return capabilities_nolock();
}

Chamber::Backend Chamber::backend() const {
#if XL_ENCLOSURE_SUPPORT()
    if (xl_enclosure.isEnabled()) {
        return Backend::xl_enclosure;
    }
#endif

#if HAS_XBUDDY_EXTENSION()
    if (xbuddy_extension().status() != XBuddyExtension::Status::disabled) {
        return Backend::xbuddy_extension;
    }
#endif

    return Backend::none;
}

std::optional<Temperature> Chamber::current_temperature() const {
    std::lock_guard _lg(mutex_);

#if PRINTER_IS_PRUSA_COREONE()
    // When using heatbreak proxy, the offset is already baked into the value
    // stored in thermistor_temperature_ (heatbreak_ema - heatbreak_chamber_offset)
    if (using_heatbreak_sensor_) {
        return thermistor_temperature_;
    }
#endif

    // XBE thermistor path — apply position offset
    const auto chamber_tempearture = thermistor_temperature_;
#if HAS_CHAMBER_TEMPERATURE_THERMISTOR_POSITION_OFFSET()
    const auto bed_temperature = thermalManager.degBed();
    static constexpr Temperature min_temp = 20.f;
    if (chamber_tempearture.has_value() && bed_temperature > *chamber_tempearture && *chamber_tempearture > min_temp) {
        static constexpr Temperature bed_max = BED_MAXTEMP - BED_MAXTEMP_SAFETY_MARGIN;
        static constexpr Temperature chamber_max = chamber_maxtemp;
        #if PRINTER_IS_PRUSA_COREONEL()
        static constexpr Temperature offset = 8.f / ((bed_max - min_temp) * std::sqrt(chamber_max - min_temp));
        #else
        static constexpr Temperature offset = 6.f / ((bed_max - min_temp) * std::sqrt(chamber_max - min_temp));
        #endif
        return chamber_tempearture.value() + offset * (bed_temperature - chamber_tempearture.value()) * std::sqrt(chamber_tempearture.value() - min_temp);
    }
#endif
    return chamber_tempearture;
}

std::optional<Temperature> Chamber::thermistor_temperature() const {
    std::lock_guard _lg(mutex_);
    return thermistor_temperature_;
}

std::optional<Temperature> Chamber::target_temperature() const {
    std::lock_guard _lg(mutex_);
    return target_temperature_;
}

std::optional<Temperature> Chamber::set_target_temperature(std::optional<Temperature> target) {
    // Wake up heaters if they are timed out
    buddy::safety_timer().reset_restore_nonblocking();

    std::lock_guard _lg(mutex_);
    target_temperature_ = target;

    const auto max_temp = capabilities_nolock().max_temp;
    if (max_temp.has_value() && target_temperature_.has_value()) {
        target_temperature_ = std::min(*target_temperature_, *max_temp);
    }

    METRIC_DEF(metric_chamber_ttemp, "chamber_ttemp", METRIC_VALUE_FLOAT, 1000, METRIC_DISABLED);
    metric_record_float(&metric_chamber_ttemp, target_temperature_.value_or(NAN));

    return target_temperature_;
}

void Chamber::reset() {
    std::lock_guard _lg(mutex_);
    target_temperature_ = std::nullopt;

#if PRINTER_IS_PRUSA_COREONE()
    using_heatbreak_sensor_ = false;
    heatbreak_ema_ = -1.0f;
    last_heatbreak_ema_ms_ = 0;
#endif

#if HAS_XBUDDY_EXTENSION()
    xbuddy_extension().set_fan_target_pwm(XBuddyExtension::Fan::cooling_fan_1, pwm_auto);
    xbuddy_extension().set_fan_target_pwm(XBuddyExtension::Fan::filtration_fan, pwm_auto);
#endif
}

#if HAS_CHAMBER_VENTS()
void Chamber::manage_ventilation_state(std::optional<Temperature> fil_target) {

    const auto control_state = config_store().get_vent_control();
    if (control_state == VentControl::off) {
        return;
    }

    constexpr uint8_t temp_limit = 45; // Limit for closed grills is chamber max temperature of PETG

    auto open = [&]() {
        if (control_state == VentControl::automatic) {
            automatic_chamber_vents::open();
        } else {
            marlin_server::set_warning(WarningType::OpenChamberVents);
            vent_state_ = Chamber::VentState::open;
        }
    };

    auto close = [&]() {
        if (control_state == VentControl::automatic) {
            automatic_chamber_vents::close();
        } else {
            marlin_server::set_warning(WarningType::CloseChamberVents);
            vent_state_ = Chamber::VentState::closed;
        }
    };

    // Don't show any vent dialog/manipulate grilles if filament doesn't support chamber temperature control
    if (fil_target.has_value()) {
        if (fil_target.value() > temp_limit && vent_state_ != Chamber::VentState::closed) {
            close();
        } else if (fil_target.value() <= temp_limit && vent_state_ != Chamber::VentState::open) {
            open();
        }
    }
}
#endif

} // namespace buddy
