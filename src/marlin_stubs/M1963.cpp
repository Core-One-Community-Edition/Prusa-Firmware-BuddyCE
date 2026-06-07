#include "PrusaGcodeSuite.hpp"
#include <feature/bed_level_probe/bed_level_probe.hpp>

#if HAS_BED_LEVEL_PROBE()

/**
 *### M1963: Bed Level Probe
 *
 * Probes a full-bed grid and reports the height offset at each point relative
 * to the point closest to the nozzle, helping diagnose bed flatness issues.
 */
void PrusaGcodeSuite::M1963() {
    bed_level_probe::run(bed_level_probe::Mode::grid);
}

#endif // HAS_BED_LEVEL_PROBE()
