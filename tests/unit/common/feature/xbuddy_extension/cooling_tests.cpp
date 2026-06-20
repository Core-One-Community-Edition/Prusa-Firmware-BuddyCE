#include <feature/xbuddy_extension/cooling.hpp>

#include <catch2/catch.hpp>

using namespace buddy;

std::ostream &operator<<(std::ostream &os, const FanCooling::FanPWM &pwm) {
    return os << "FanPWM{" << static_cast<int>(pwm.value) << "}";
}

TEST_CASE("Cooling PWM") {
    buddy::FanCooling cooling;
    static constexpr buddy::FanCooling::FanPWM max_auto_pwm { 100 };

    const auto step = [&](bool already_spinning, Temperature current_temperature, std::optional<Temperature> target_temperature, PWM255OrAuto target_pwm, std::optional<Temperature> heatbreak_temperature = std::nullopt) {
        auto result = cooling.compute_pwm_step(current_temperature, target_temperature, target_pwm, max_auto_pwm, heatbreak_temperature);
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
        const std::optional<Temperature> target_temperature = 20;
        const Temperature current_temperature = 55;

        const auto result = step(true, current_temperature, target_temperature, pwm_auto);
        REQUIRE(result > PWM255 { 0 });
        for (uint32_t i = 0; i < 10; i++) {
            step(true, current_temperature, target_temperature, pwm_auto);
        }
        REQUIRE(step(true, current_temperature, target_temperature, pwm_auto) == max_auto_pwm);
    }

    SECTION("Nonsense range test") {
        const Temperature current_temperature = 55;

        REQUIRE(step(true, current_temperature, -100, pwm_auto) == max_auto_pwm);

        // due to previous regulation cycle, the target value must be multiplied
        REQUIRE(step(true, current_temperature, 300, pwm_auto) == PWM255 { 0 });
    }

    SECTION("Fan kick up speed") {
        Temperature target_temperature = 20;

        // Use a small error that gets clamped to min_pwm
        // Error of 4°C * ramp_slope(10) = 40 PWM, which equals min_pwm after apply_pwm_overrides
        REQUIRE(step(false, 4.0 + target_temperature, target_temperature, pwm_auto) == cooling.spin_up_pwm);

        REQUIRE(step(true, 4.0 + target_temperature, target_temperature, pwm_auto) == cooling.min_pwm);
    }

    SECTION("Heatbreak limiter") {
        const Temperature heatbreak_limit = 45;
        const std::optional<Temperature> target_temperature = 20;
        const Temperature current_temperature = 35;

        cooling.heatbreak_max_temp = heatbreak_limit;

        SECTION("Heatbreak below limit, no boost") {
            // Saturate the chamber regulation at max_auto_pwm first
            for (uint32_t i = 0; i < 20; i++) {
                step(true, current_temperature, target_temperature, pwm_auto, heatbreak_limit - 5);
            }

            REQUIRE(step(true, current_temperature, target_temperature, pwm_auto, heatbreak_limit - 5) == max_auto_pwm);
            REQUIRE(cooling.get_heatbreak_pwm_boost() == PWM255 { 0 });
        }

        SECTION("Heatbreak over limit, boost exceeds max_auto_pwm") {
            // Boost integrates by 2.5 PWM per step at 5°C error -> enough steps to pass max_auto_pwm
            for (uint32_t i = 0; i < 50; i++) {
                step(true, current_temperature, target_temperature, pwm_auto, heatbreak_limit + 5);
            }

            const auto boosted = step(true, current_temperature, target_temperature, pwm_auto, heatbreak_limit + 5);
            REQUIRE(boosted > max_auto_pwm);

            // The boost is clamped at max_pwm
            for (uint32_t i = 0; i < 200; i++) {
                step(true, current_temperature, target_temperature, pwm_auto, heatbreak_limit + 5);
            }

            REQUIRE(step(true, current_temperature, target_temperature, pwm_auto, heatbreak_limit + 5) == cooling.max_pwm);

            SECTION("Heatbreak recovers, boost decays back") {
                for (uint32_t i = 0; i < 500; i++) {
                    step(true, current_temperature, target_temperature, pwm_auto, heatbreak_limit - 5);
                }

                REQUIRE(step(true, current_temperature, target_temperature, pwm_auto, heatbreak_limit - 5) == max_auto_pwm);
                REQUIRE(cooling.get_heatbreak_pwm_boost() == PWM255 { 0 });
            }

            SECTION("Manual fan PWM resets the boost") {
                step(true, current_temperature, target_temperature, PWM255 { 50 }, heatbreak_limit + 5);

                REQUIRE(cooling.get_heatbreak_pwm_boost() == PWM255 { 0 });
            }

            SECTION("Disabling the limiter resets the boost") {
                cooling.heatbreak_max_temp = std::nullopt;

                step(true, current_temperature, target_temperature, pwm_auto, heatbreak_limit + 5);

                REQUIRE(cooling.get_heatbreak_pwm_boost() == PWM255 { 0 });
            }
        }

        SECTION("Boost works without a chamber target temperature") {
            for (uint32_t i = 0; i < 20; i++) {
                step(true, current_temperature, std::nullopt, pwm_auto, heatbreak_limit + 5);
            }

            REQUIRE(step(true, current_temperature, std::nullopt, pwm_auto, heatbreak_limit + 5) > PWM255 { 0 });
        }
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
