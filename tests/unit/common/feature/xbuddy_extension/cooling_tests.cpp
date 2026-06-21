#include <feature/xbuddy_extension/cooling.hpp>

#include <catch2/catch.hpp>

using namespace buddy;

std::ostream &operator<<(std::ostream &os, const FanCooling::FanPWM &pwm) {
    return os << "FanPWM{" << static_cast<int>(pwm.value) << "}";
}

TEST_CASE("Cooling PWM") {
    buddy::FanCooling cooling;
    static constexpr buddy::FanCooling::FanPWM max_auto_pwm { 100 };

    const auto step = [&](bool already_spinning, Temperature current_temperature, std::optional<Temperature> target_temperature, PWM255OrAuto target_pwm) {
        auto result = cooling.compute_pwm_step(current_temperature, target_temperature, target_pwm, max_auto_pwm);
        result = cooling.apply_pwm_overrides(already_spinning, result);
        return result;
    };

    SECTION("Manual, full pwm") {
        std::optional<Temperature> target_temperature;
        SECTION("With target temp") {
            target_temperature = 60;
        }

        SECTION("Without target temp") {}

        REQUIRE(step(true, 54, target_temperature, cooling.max_pwm) == cooling.max_pwm);
        REQUIRE(step(false, 54, target_temperature, cooling.max_pwm) == cooling.max_pwm);
    }

    SECTION("Manual, low PWM") {
        const PWM255 target_pwm { 5 };

        SECTION("Already running") {
            REQUIRE(step(true, 54, std::nullopt, target_pwm) == cooling.min_pwm);
        }

        SECTION("Initial kick") {
            REQUIRE(step(false, 54, std::nullopt, target_pwm) == cooling.spin_up_pwm);
        }
    }

    SECTION("Auto cooling, no target temp") {
        REQUIRE(step(true, 45, std::nullopt, pwm_auto).value == 0);
    }

    SECTION("Auto cooling, cold chamber") {
        REQUIRE(step(false, 20, 60, pwm_auto).value == 0);
    }

    SECTION("Auto cooling, slightly cool") {
        SECTION("Not running, don't start") {
            REQUIRE(step(false, 59, 60, pwm_auto).value == 0);
        }
    }

    SECTION("Auto cooling, really hot") {
        // Far above target - the ramp regulator saturates at the max allowed PWM right away
        REQUIRE(step(true, 55, 20, pwm_auto) == max_auto_pwm);
    }

    SECTION("Auto cooling, graduated curve") {
        const std::optional<Temperature> target_temperature = 40;

        // Slightly above target - below the hysteresis turn-on threshold, fans stay off
        REQUIRE(step(true, 42, target_temperature, pwm_auto) == PWM255 { 0 });
        REQUIRE(step(true, 43, target_temperature, pwm_auto) == PWM255 { 0 });

        // Error of 4°C * default_ramp_slope(10) = 40 PWM, above the turn-on threshold
        REQUIRE(step(true, 44, target_temperature, pwm_auto) == PWM255 { 40 });

        // Hysteresis dead-band - once running, a drop below the turn-on threshold keeps the fans on
        REQUIRE(step(true, 43, target_temperature, pwm_auto) == cooling.min_pwm);

        // Below the turn-off threshold, fans turn off
        REQUIRE(step(true, 42, target_temperature, pwm_auto) == PWM255 { 0 });

        // Proportional region - 10 PWM per °C above target
        REQUIRE(step(true, 47, target_temperature, pwm_auto) == PWM255 { 70 });

        // Far above target - clamped to the max allowed PWM
        REQUIRE(step(true, 55, target_temperature, pwm_auto) == max_auto_pwm);
    }

    SECTION("Auto cooling, dormant legacy regulator") {
        cooling.regulator_legacy = true;

        // The legacy I-only regulator winds up to the max allowed PWM on sustained overtemperature
        for (uint32_t i = 0; i < 10; i++) {
            step(true, 50, 40, pwm_auto);
        }
        REQUIRE(step(true, 50, 40, pwm_auto) == max_auto_pwm);
    }

    SECTION("Nonsense range test") {
        const Temperature current_temperature = 55;

        REQUIRE(step(true, current_temperature, -100, pwm_auto) == max_auto_pwm);

        // Target far above the current temperature - no cooling needed
        REQUIRE(step(true, current_temperature, 300, pwm_auto) == PWM255 { 0 });
    }

    SECTION("Fan kick up speed") {
        Temperature target_temperature = 20;

        // Use a small error that gets clamped to min_pwm
        // Error of 4°C * ramp_slope(10) = 40 PWM, which equals min_pwm after apply_pwm_overrides
        REQUIRE(step(false, 4.0 + target_temperature, target_temperature, pwm_auto) == cooling.spin_up_pwm);

        REQUIRE(step(true, 4.0 + target_temperature, target_temperature, pwm_auto) == cooling.min_pwm);
    }

    SECTION("Overheating cooling") {
        SECTION("Full power") {
            const Temperature target_temperature = 20;
            REQUIRE(step(true, cooling.overheating_temp, target_temperature, pwm_auto) == cooling.max_pwm);
            REQUIRE(cooling.get_overheating_temp_flag());
            REQUIRE(step(true, cooling.recovery_temp + 1.0, target_temperature, pwm_auto) == cooling.max_pwm);

            step(true, cooling.recovery_temp - 1.0, target_temperature, pwm_auto); // one more regulation cycle is needed to recover
            REQUIRE(step(true, cooling.recovery_temp - 1.0, target_temperature, pwm_auto) == max_auto_pwm);

            REQUIRE(step(true, cooling.critical_temp, target_temperature, pwm_auto) == cooling.max_pwm);
            REQUIRE(cooling.get_critical_temp_flag());

            REQUIRE(step(true, cooling.recovery_temp + 1.0, target_temperature, pwm_auto) == cooling.max_pwm);
            step(true, cooling.recovery_temp - 1.0, target_temperature, pwm_auto); // one regulation cycle is needed to recover
            REQUIRE(step(true, cooling.recovery_temp - 1.0, target_temperature, pwm_auto) == max_auto_pwm);
        }
    }
}
