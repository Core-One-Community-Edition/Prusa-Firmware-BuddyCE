#include "config_features.h"
#include <feature/filament_sensor/filament_sensors_handler.hpp>
#include <config_store/store_instance.hpp>
#include <filament_to_load.hpp>

// clang-format off
#if (!ENABLED(FILAMENT_LOAD_UNLOAD_GCODES)) || \
    ENABLED(NO_MOTION_BEFORE_HOMING)
    #error unsupported
#endif
// clang-format on

#include "../../../lib/Marlin/Marlin/src/Marlin.h"
#include "../../../lib/Marlin/Marlin/src/module/motion.h"
#include "../../../lib/Marlin/Marlin/src/module/planner.h"
#include "../../../lib/Marlin/Marlin/src/module/temperature.h"
#include "pause_stubbed.hpp"
#include <feature/filament_sensor/filament_sensors_handler.hpp>
#include "M70X.hpp"
#include <marlin_vars.hpp>

#if HAS_CHAMBER_API()
    #include <feature/chamber/chamber.hpp>
#endif

static FSMResponseVariant preheatTempKnown(uint8_t target_extruder) {
    const FilamentType filament_type = config_store().get_filament_type(target_extruder);
    assert(filament_type != FilamentType::none);
    return FSMResponseVariant::make(filament_type);
}

static FSMResponseVariant preheatTempUnKnown(PreheatData preheat_data, bool break_on_autoload = false) {
    marlin_server::FSM_Holder holder { PhasesPreheat::UserTempSelection, preheat_data.serialize() };

    while (true) {
        if (const auto ret = marlin_server::get_response_variant_from_phase(PhasesPreheat::UserTempSelection)) {
            return ret;
        }
        if (preheat_data.mode == PreheatMode::Autoload && FSensors_instance().sensor_state(LogicalFilamentSensor::primary_runout) == FilamentSensorState::NoFilament) {
            return FSMResponseVariant::make(Response::Abort);
        }
        if (break_on_autoload && FSensors_instance().IsAutoloadInProgress()) {
            return FSMResponseVariant();
        }

        idle(true);
    }
}

static FSMResponseVariant evaluate_preheat_conditions(PreheatData preheat_data, uint8_t target_extruder) {
    bool canKnowTemp = preheat_data.mode == PreheatMode::Unload || preheat_data.mode == PreheatMode::Change_phase1 || preheat_data.mode == PreheatMode::Purge || preheat_data.mode == PreheatMode::Unload_askUnloaded;

    // Check if we are using operation which can get temp from printer and check if it can get the temp from available info (inserted filament or set temperature in temperature menu and no filament inserted)
    if (canKnowTemp && ((config_store().get_filament_type(target_extruder) != FilamentType::none))) {
        // We can get temperature without user telling us
        return preheatTempKnown(target_extruder);

    } else {
        // we need to ask the user for temperature
        return preheatTempUnKnown(preheat_data);
    }
}

std::pair<std::optional<PreheatStatus::Result>, FilamentType> filament_gcodes::preheat(PreheatData preheat_data, uint8_t target_extruder, PreheatBehavior preheat_arg) {
    const FSMResponseVariant response = evaluate_preheat_conditions(preheat_data, target_extruder);

    if (response.holds_alternative<FilamentType>()) {
        const FilamentType filament = response.value<FilamentType>();
        preheat_to(filament, target_extruder, preheat_arg);
        return { std::nullopt, filament };
    }

    switch (response.value_or<Response>(Response::_none)) {

    case Response::Abort:
        return { PreheatStatus::Result::Aborted, FilamentType::none };

    case Response::Cooldown:
        return { PreheatStatus::Result::CooledDown, FilamentType::none };

    default:
        // should not happen
        return { PreheatStatus::Result::Error, FilamentType::none };
    }
}

static uint8_t display_temp_extruder([[maybe_unused]] uint8_t target_extruder) {
#if HAS_MMU2()
    // MMU has multiple slots (target_extruder can be >0) but only a single nozzle
    // -> we need the correct temperature per active slot
    // but we also need marlin_server::set_temp_to_display not to crash due to target_extruder > 0
    return 0;
#else
    return target_extruder;
#endif
}

void filament_gcodes::preheat_to(FilamentType filament, uint8_t target_extruder, PreheatBehavior preheat_arg) {
    const FilamentTypeParameters fil_cnf = filament.parameters();

    int16_t target_temp = fil_cnf.nozzle_temperature;

    // If the previously loaded filament had a higher nozzle temperature,
    // preheat to that temperature to ensure the remnants in the nozzle
    // will melt enough to allow the load.
    //
    // If the previously loaded filament is unknown, preheat to 280C
    // for the case there was a high-temperature filament in the nozzle
    // previously.
    //
    // Only do this if there's no currently loaded filament to avoid
    // preheating to 280C if we're already loaded.
    if (!config_store().get_filament_type(target_extruder)) {
        int16_t prev_temp = 280; // default previous filament temperature in case the previous filament is unknown
        const FilamentType prev_filament = config_store().get_previous_filament_type(target_extruder);
        if (prev_filament != FilamentType::none) {
            prev_temp = prev_filament.parameters().nozzle_temperature;
        }
        target_temp = std::max(target_temp, prev_temp);
    }

    // change temp only if it is lower than currently loaded filament
    if (preheat_arg.force_temp || thermalManager.degTargetHotend(target_extruder) < target_temp) {
        thermalManager.setTargetHotend(target_temp, target_extruder);

        marlin_server::set_temp_to_display(target_temp, display_temp_extruder(target_extruder));
        if (preheat_arg.preheat_bed && (preheat_arg.force_temp || (thermalManager.degTargetBed() < fil_cnf.heatbed_temperature))) {
            thermalManager.setTargetBed(fil_cnf.heatbed_temperature);
        }
    }

#if HAS_CHAMBER_API()
    if (preheat_arg.set_chamber_temperature) {
        buddy::chamber().set_target_temperature(fil_cnf.chamber_target_temperature);
    }
#endif

#if HAS_FILAMENT_HEATBREAK_PARAM()
    thermalManager.setTargetHeatbreak(fil_cnf.heatbreak_temperature, target_extruder);
#endif
}

filament_gcodes::NozzlePreheatState filament_gcodes::capture_nozzle_preheat_state(uint8_t target_extruder) {
    return NozzlePreheatState {
        .target = static_cast<float>(thermalManager.degTargetHotend(target_extruder)),
        .display = marlin_vars().hotend(display_temp_extruder(target_extruder)).display_nozzle,
    };
}

void filament_gcodes::retrigger_preheat_after_load(const NozzlePreheatState &pre_load_state, FilamentType loaded_filament, uint8_t target_extruder) {
    // The preheat must have been for the filament that was just loaded
    if (filament::get_preheated_type() != loaded_filament) {
        return;
    }

    // ... and must have still been active before the load: nozzle held at the standby temperature
    // with the full temperature on display (e.g. not ended by the safety timer or a manual temperature change)
    const FilamentTypeParameters fil_cnf = loaded_filament.parameters();
    const bool preheat_was_active = pre_load_state.target == static_cast<float>(fil_cnf.nozzle_preheat_temperature)
        && pre_load_state.display == static_cast<float>(fil_cnf.nozzle_temperature);
    if (!preheat_was_active) {
        return;
    }

    // The load ended in the standby state, trigger the preheat again so the preheat sequence continues
    M1700_apply_preheat(loaded_filament, M1700Args {
        .preheat = RetAndCool_t::Neither,
        .mode = PreheatMode::None,
        .target_extruder = static_cast<int8_t>(target_extruder),
        .save = false,
        .enforce_target_temp = false,
        .preheat_bed = true,
#if HAS_CHAMBER_API()
        .preheat_chamber = true,
#endif
#if HAS_FILAMENT_HEATBREAK_PARAM()
        .set_heatbreak = true,
#endif
    });
}

std::pair<std::optional<PreheatStatus::Result>, FilamentType> filament_gcodes::preheat_for_change_load(PreheatData data, uint8_t target_extruder) {
    const FSMResponseVariant response = preheatTempUnKnown(data);

    if (response.holds_alternative<FilamentType>()) {
        const FilamentType filament = response.value<FilamentType>();
        // change temp every time (unlike normal preheat)
        preheat_to(filament, target_extruder, PreheatBehavior::force_preheat_bed_and_chamber(config_store().filament_change_preheat_all.get()));
        return { std::nullopt, filament };
    }

    switch (response.value_or<Response>(Response::_none)) {

    case Response::Abort:
        return { PreheatStatus::Result::Aborted, FilamentType::none };

    case Response::Cooldown:
        return { PreheatStatus::Result::CooledDown, FilamentType::none };

    default:
        // should not happen
        return { PreheatStatus::Result::Error, FilamentType::none };
    }
}

void filament_gcodes::M1700_apply_preheat(FilamentType filament, const M1700Args &args) {
    const FilamentTypeParameters fil_cnf = filament.parameters();

    // Remember what the user is preheating for, so that a subsequent load can preselect it.
    // Cooldown yields FilamentType::none, clearing the hint.
    filament::set_preheated_type(filament);

    const auto set_extruder_temp = [&](uint8_t extruder) {
        thermalManager.setTargetHotend(args.enforce_target_temp ? fil_cnf.nozzle_temperature : fil_cnf.nozzle_preheat_temperature, extruder);
        marlin_server::set_temp_to_display(fil_cnf.nozzle_temperature, extruder);
    };

    if (filament == FilamentType::none || args.target_extruder < 0) {
        // Set temperature to all tools
        // Cooldown (filament none) is always applied to all tools
        for (int8_t e = 0; e < HOTENDS; e++) {
#if ENABLED(PRUSA_TOOLCHANGER)
            if (!prusa_toolchanger.is_tool_enabled(e)) {
                continue;
            }
#endif /*ENABLED(PRUSA_TOOLCHANGER)*/
            set_extruder_temp(e);
        }

    } else {
        // Preheat only target tool
        set_extruder_temp(args.target_extruder);
    }

    if (args.preheat_bed) {
        thermalManager.setTargetBed(fil_cnf.heatbed_temperature);
    }

#if HAS_CHAMBER_API()
    if (args.preheat_chamber) {
        buddy::chamber().set_target_temperature(fil_cnf.chamber_target_temperature);
    #if HAS_CHAMBER_VENTS()
        buddy::chamber().manage_ventilation_state(fil_cnf.chamber_target_temperature);
    #endif
    }
#endif

#if HAS_FILAMENT_HEATBREAK_PARAM()
    if (args.set_heatbreak) {
        thermalManager.setTargetHeatbreak(fil_cnf.heatbreak_temperature, args.target_extruder);
    }
#endif
}

void filament_gcodes::M1700_preheat(const M1700Args &args) {
    InProgress progress;
    const FSMResponseVariant response_variant = preheatTempUnKnown(PreheatData::make(args.mode, args.target_extruder, args.preheat), true);

    // autoload ocurred
    if (!response_variant) {
        return;
    }

    const Response response = response_variant.value_or<Response>(Response::_none);
    if (response == Response::Abort) {
        PreheatStatus::SetResult(PreheatStatus::Result::Aborted);
        return;
    }

    const FilamentType filament = response_variant.value_or<FilamentType>(FilamentType::none);

    M1700_apply_preheat(filament, args);

    // cooldown pressed
    if (filament == FilamentType::none) {
        thermalManager.set_fan_speed(0, 0);

    } else if (!axes_home_level.is_homed(Z_AXIS, AxisHomeLevel::imprecise)) {
        unhomed_z_lift(10);
    }

    if (args.save) {
        if (args.target_extruder < 0) {
            for (int8_t e = 0; e < HOTENDS; e++) {
                config_store().set_filament_type(e, filament);
            }
        } else {
            config_store().set_filament_type(args.target_extruder, filament);
        }
    }

    // store result, so other threads can see it
    PreheatStatus::SetResult(PreheatStatus::Result::DoneNoFilament);
}
