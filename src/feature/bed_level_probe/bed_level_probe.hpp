#pragma once

#include <option/has_bed_level_probe.h>

#if HAS_BED_LEVEL_PROBE()

#include <cstdint>

namespace bed_level_probe {

enum class Mode : uint8_t {
    shims, // Probe the three Z lead-screw mounts (user-facing).
    grid, // Probe a full-bed grid (dev only).
};

inline constexpr uint8_t grid_cols = 8;
inline constexpr uint8_t grid_rows = 6;
inline constexpr uint8_t max_points = grid_cols * grid_rows;

struct ProbePoint {
    float x;
    float y;
    float z; // NAN until probed.
};

// Data shared between the logic thread and the GUI screen.
// The FSM lifecycle guarantees there is no concurrent access.
struct ProbeData {
    Mode mode;
    uint8_t cols; // Grid columns (0 in shims mode).
    uint8_t rows; // Grid rows (0 in shims mode).
    uint8_t count; // Number of active points in points[].
    uint8_t probed; // Number of points probed so far.
    ProbePoint points[max_points]; // Stored row-major in grid mode.
};

extern ProbeData probe_data;

// Highest probed Z (the point closest to the nozzle) used as the zero
// reference. Returns NAN when nothing has been probed yet.
float reference_z();

void run(Mode mode);

} // namespace bed_level_probe

#endif // HAS_BED_LEVEL_PROBE()
