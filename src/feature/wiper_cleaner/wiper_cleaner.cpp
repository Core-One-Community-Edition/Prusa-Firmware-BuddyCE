#include "wiper_cleaner.hpp"
#include "Marlin/src/gcode/gcode.h"
#include "raii/scope_guard.hpp"
#include <gcode_loader.hpp>
#include <Marlin/src/module/motion.h>

namespace wiper_cleaner {

// Resolved by GCodeLoader to `/usb/macros/wiper/wipe.gcode`.
ConstexprString wipe_filename = "wiper/wipe";

static GCodeLoader &wiper_gcode_loader_instance() {
    static GCodeLoader wiper_gcode_loader;
    return wiper_gcode_loader;
}

void load_wipe_gcode() {
    wiper_gcode_loader_instance().load_gcode(wipe_filename, nullptr);
}

bool is_loader_idle() {
    return wiper_gcode_loader_instance().is_idle();
}

bool is_loader_buffering() {
    return wiper_gcode_loader_instance().is_buffering();
}

bool execute() {
    // Nothing to execute yet - not loaded or still buffering. Do not reset
    // while buffering so the async load can complete on a later call.
    if (is_loader_idle() || is_loader_buffering()) {
        return false;
    }

    // Without XY homed we cannot safely run the wipe moves. Bail out (and reset
    // so the loader is ready next time) - bed mesh leveling will home as usual.
    if (!(axes_home_level.is_homed(X_AXIS, AxisHomeLevel::imprecise) && axes_home_level.is_homed(Y_AXIS, AxisHomeLevel::imprecise))) {
        reset();
        return false;
    }

    auto loader_result = wiper_gcode_loader_instance().get_result();
    ScopeGuard reset_loader = [&] { reset(); };

    if (loader_result.has_value()) {
        GcodeSuite::process_subcommands_now(loader_result.value());
        return true;
    }

    return false;
}

void reset() {
    wiper_gcode_loader_instance().reset();
}

} // namespace wiper_cleaner
