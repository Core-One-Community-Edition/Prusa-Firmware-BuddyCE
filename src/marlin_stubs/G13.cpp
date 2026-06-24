#include "PrusaGcodeSuite.hpp"

#include <wiper_cleaner.hpp>
#include <config_store/store_instance.hpp>
#include <logging/log.hpp>
#include "marlin_server.hpp"

LOG_COMPONENT_REF(PRUSA_GCODE);

/** \addtogroup G-Codes
 * @{
 */

/**
 * ### G13: Wipe nozzle on bed wiper
 *
 * Loads and executes the user-supplied wiping g-code from the USB drive
 * (`/usb/macros/wiper/wipe.gcode`), cleaning the nozzle on the silicone wiper
 * mounted on the front of the heatbed.
 *
 * If the file is missing or fails to load, nothing happens.
 *
 * Distinct from `G12` (iX nozzle cleaner); the two may be unified in the
 * future once a printer carries both mechanisms.
 *
 * #### Usage
 *
 *     G13
 *
 */

void PrusaGcodeSuite::G13() {
    // Honors the hardware-menu enable toggle. When disabled, do nothing.
    if (!config_store().bed_wiper_enable.get()) {
        return;
    }
    // Load and run the user-supplied wiping g-code. If the file is absent,
    // execute() returns false and nothing happens.
    if (wiper_cleaner::is_loader_idle()) {
        wiper_cleaner::load_wipe_gcode();
    }
    while (wiper_cleaner::is_loader_buffering()) {
        idle(true);
    }
    wiper_cleaner::execute();
}

/** @}*/
