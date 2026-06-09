#include "PrusaGcodeSuite.hpp"
#include <feature/bed_level_probe/bed_level_probe.hpp>

#if HAS_BED_LEVEL_PROBE()

/**
 *### M1963: Bed Level Probe
 *
 * Probes a full-bed grid and reports the height offset at each point relative
 * to the point closest to the nozzle, helping diagnose bed flatness issues.
 *
 *#### Parameters
 *
 * - `A` - Auto mode: skip intro/results screens, probe and report immediately.
 *          Intended for automated/serial-driven measurements.
 */
void PrusaGcodeSuite::M1963() {
    const bool interactive = !parser.seen('A');
    bed_level_probe::run(bed_level_probe::Mode::grid, interactive);
    bed_level_probe::report_serial();
}

#endif // HAS_BED_LEVEL_PROBE()
