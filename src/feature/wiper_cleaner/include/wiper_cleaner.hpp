#pragma once

/**
 * @brief Bed wiper - cleans the nozzle on a silicone wiper mounted on the front
 *        of the heatbed before bed mesh leveling.
 *
 * The wiping g-code is supplied by the user as a file on the USB drive
 * (`/usb/macros/wiper/wipe.gcode`). The firmware only loads and injects it into
 * Marlin's g-code stream; the actual motion is defined entirely by that file.
 * G-code is treated as the API between this module and Marlin - no motion code
 * is shared.
 *
 * If the file is missing or fails to load, the wiper is silently skipped and
 * bed mesh leveling proceeds exactly as it would without a wiper.
 */

#include <str_utils.hpp>

namespace wiper_cleaner {

/// USB path (relative to `/usb/macros/`) of the user-supplied wiping g-code.
/// Resolves to `/usb/macros/wiper/wipe.gcode`.
extern ConstexprString wipe_filename;

/// Begin asynchronously loading the wiping g-code from the USB drive.
/// Must be in idle state (see is_loader_idle()).
void load_wipe_gcode();

bool is_loader_idle();
bool is_loader_buffering();

/**
 * @brief Execute the loaded wiping g-code.
 *
 * load_wipe_gcode() must be called first and the loader must have finished
 * buffering (is_loader_buffering() == false).
 *
 * @retval true  the wiping g-code was executed successfully
 * @retval false the g-code was not loaded, is still buffering, failed to load
 *               (e.g. file absent) or XY is not homed. In all these cases bed
 *               mesh leveling should simply proceed as usual.
 */
bool execute();

/// Reset the loader back to idle so it can be used again.
void reset();

} // namespace wiper_cleaner
