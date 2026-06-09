#include "PrusaGcodeSuite.hpp"
#include <feature/bed_level_probe/bed_level_probe.hpp>

#if HAS_BED_LEVEL_PROBE()

/**
 *### M1962: Shim Calibration
 *
 * Probes the three Z lead-screw positions and reports the height offset of
 * each relative to the point closest to the nozzle, recommending shims.
 *
 *#### Parameters
 *
 * - `A` - Auto mode: skip intro/results screens, probe and report immediately.
 *          Intended for automated/serial-driven measurements.
 */
void PrusaGcodeSuite::M1962() {
    const bool interactive = !parser.seen('A');
    bed_level_probe::run(bed_level_probe::Mode::shims, interactive);
    bed_level_probe::report_serial();
}

#endif // HAS_BED_LEVEL_PROBE()
