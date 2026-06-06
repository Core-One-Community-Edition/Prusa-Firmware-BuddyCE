#include "PrusaGcodeSuite.hpp"

#include <option/xbuddy_extension_variant.h>

#if XBUDDY_EXTENSION_VARIANT_IS_STANDARD()

    #include <feature/xbuddy_extension/xbuddy_extension.hpp>
    #include <gcode/gcode_parser.hpp>

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M9160: Set maximum heatbreak temperature
 *
 * Only available on printers with the XBuddy Extension chamber cooling fans.
 *
 *#### Usage
 *
 *    M9160 [ S ]
 *
 *#### Parameters
 *
 * - `S` - Maximum heatbreak temperature in degrees Celsius. 0 = disable the heatbreak limiter
 *
 * Without parameters, reports the current limit and chamber fan boost.
 *
 * When the heatbreak temperature exceeds the limit, the chamber cooling fans are gradually
 * ramped up - beyond the user max fan limit if necessary - to lower the chamber temperature
 * until the heatbreak temperature recovers. The fans must be in auto mode for the limiter
 * to take effect. The limit is cleared on print start and print end.
 *
 * Note that a limit below the chamber temperature the fans can possibly reach keeps them
 * at full power for the rest of the print.
 */
void PrusaGcodeSuite::M9160() {
    using buddy::Temperature;

    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }

    if (const auto temp = p.option<Temperature>('S')) {
        if (*temp > 0) {
            buddy::xbuddy_extension().set_heatbreak_max_temp(*temp);
        } else {
            buddy::xbuddy_extension().set_heatbreak_max_temp(std::nullopt);
        }
        return;
    }

    const auto limit = buddy::xbuddy_extension().heatbreak_max_temp();
    if (limit.has_value()) {
        SERIAL_ECHOLNPAIR("Heatbreak max temp: ", *limit, ", chamber fan boost: ", static_cast<int>(buddy::xbuddy_extension().heatbreak_pwm_boost().value));
    } else {
        SERIAL_ECHOLNPGM("Heatbreak max temp limiter disabled");
    }
}

/** @}*/

#endif
