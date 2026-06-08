#include <feature/bed_level_probe/bed_level_probe.hpp>

#if HAS_BED_LEVEL_PROBE()

#include <client_response.hpp>
#include <common/fsm_base_types.hpp>
#include <marlin_server.hpp>
#include <calibration_z.hpp>
#include <module/probe.h>
#include <gcode/gcode.h>
#include <module/motion.h>
#include <buddy/unreachable.hpp>
#include <cmath>
#include <iterator>

static_assert(HAS_BED_LEVEL_PROBE());

namespace bed_level_probe {

ProbeData probe_data {};

namespace {

// Grid probe area: the outer points sit at the very edges of the probeable
// area and the rest are spaced evenly between them.
constexpr float grid_min_x = MESH_MIN_X;
constexpr float grid_max_x = MESH_MAX_X;
constexpr float grid_min_y = MESH_MIN_Y;
constexpr float grid_max_y = MESH_MAX_Y;

// Height the nozzle is raised to once probing has finished.
constexpr float safe_z = 50.f;

// Probe positions of the three Z lead-screw mounts. The screws hold the bed on
// mounts at the front-left and front-right corners and along the back, so we
// probe each corner / the back edge as close to the mount as the bed allows.
struct NamedPoint {
    float x;
    float y;
};
constexpr NamedPoint screw_positions[] = {
    { grid_min_x, grid_min_y }, // Left front corner.
    { grid_max_x, grid_min_y }, // Right front corner.
    { (grid_min_x + grid_max_x) / 2.f, grid_max_y }, // Back center.
};

void setup_shim_points() {
    probe_data.cols = 0;
    probe_data.rows = 0;
    probe_data.count = std::size(screw_positions);
    for (uint8_t i = 0; i < probe_data.count; ++i) {
        probe_data.points[i] = { screw_positions[i].x, screw_positions[i].y, NAN };
    }
}

void setup_grid_points() {
    probe_data.cols = grid_cols;
    probe_data.rows = grid_rows;
    probe_data.count = grid_cols * grid_rows;

    for (uint8_t row = 0; row < grid_rows; ++row) {
        const float y = grid_min_y + row * (grid_max_y - grid_min_y) / (grid_rows - 1);
        for (uint8_t col = 0; col < grid_cols; ++col) {
            const float x = grid_min_x + col * (grid_max_x - grid_min_x) / (grid_cols - 1);
            probe_data.points[row * grid_cols + col] = { x, y, NAN };
        }
    }
}

void clear_results() {
    probe_data.probed = 0;
    for (uint8_t i = 0; i < probe_data.count; ++i) {
        probe_data.points[i].z = NAN;
    }
}

} // namespace

float reference_z() {
    float max_z = NAN;
    for (uint8_t i = 0; i < probe_data.count; ++i) {
        const float z = probe_data.points[i].z;
        if (!isnan(z) && (isnan(max_z) || z > max_z)) {
            max_z = z;
        }
    }
    return max_z;
}

namespace {

using marlin_server::wait_for_response;

class BedLevelProbe {
public:
    void run() {
        do {
            run_current_phase();
        } while (curr_phase != PhaseBedLevelProbe::finish);
    }

private:
    void fsm_change(PhaseBedLevelProbe phase, fsm::PhaseData data = {}) {
        marlin_server::fsm_change(phase, data);
        curr_phase = phase;
    }

    void run_current_phase() {
        switch (curr_phase) {
        case PhaseBedLevelProbe::intro:
            intro();
            break;
        case PhaseBedLevelProbe::homing:
            homing();
            break;
        case PhaseBedLevelProbe::probing:
            probing();
            break;
        case PhaseBedLevelProbe::results:
            results();
            break;
        case PhaseBedLevelProbe::error:
            error();
            break;
        case PhaseBedLevelProbe::finish:
            BUDDY_UNREACHABLE();
            break;
        }
    }

    void start_probing() {
        // Z alignment of the gantry only matters for the shim calibration; the
        // grid probe just reports the bed shape, so it skips straight to homing.
        if (probe_data.mode == Mode::shims) {
            align_z();
        }
        fsm_change(PhaseBedLevelProbe::homing);
    }

    void intro() {
        switch (wait_for_response(curr_phase)) {
        case Response::Continue:
            start_probing();
            break;
        case Response::Abort:
            fsm_change(PhaseBedLevelProbe::finish);
            break;
        default:
            BUDDY_UNREACHABLE();
            break;
        }
    }

    // calib_Z drives the Selftest FSM internally, so it needs that FSM open
    // (mirroring G162). The Selftest "Calibrating Z" screen is shown during
    // alignment, then our wizard resumes.
    void align_z() {
        marlin_server::FSM_Holder selftest_holder { PhasesSelftest::CalibZ };
        selftest::calib_Z(false);
    }

    void homing() {
        if (!GcodeSuite::G28_no_parser(true, true, true, G28Flags {})) {
            fsm_change(PhaseBedLevelProbe::error);
            return;
        }
        clear_results();
        fsm_change(PhaseBedLevelProbe::probing);
    }

    void probe_point(uint8_t index) {
        const xy_pos_t pos = { probe_data.points[index].x, probe_data.points[index].y };
        const float z = probe_at_point(pos, PROBE_PT_RAISE, 0, false);
        probe_data.points[index].z = z;
        ++probe_data.probed;

        // Push progress so the screen fills in the probed points live.
        fsm::PhaseData data {};
        data[0] = probe_data.probed;
        fsm_change(PhaseBedLevelProbe::probing, data);
    }

    void probing() {
        if (probe_data.mode == Mode::grid) {
            // Serpentine ordering minimises travel between rows while the points
            // stay stored row-major for the display.
            for (uint8_t row = 0; row < probe_data.rows; ++row) {
                for (uint8_t step = 0; step < probe_data.cols; ++step) {
                    const uint8_t col = (row % 2 == 0) ? step : (probe_data.cols - 1 - step);
                    probe_point(row * probe_data.cols + col);
                }
            }
        } else {
            for (uint8_t i = 0; i < probe_data.count; ++i) {
                probe_point(i);
            }
        }

        do_blocking_move_to_z(safe_z);

        bool any_failed = false;
        for (uint8_t i = 0; i < probe_data.count; ++i) {
            if (isnan(probe_data.points[i].z)) {
                any_failed = true;
                break;
            }
        }

        fsm_change(any_failed ? PhaseBedLevelProbe::error : PhaseBedLevelProbe::results);
    }

    void results() {
        switch (wait_for_response(curr_phase)) {
        case Response::Done:
            fsm_change(PhaseBedLevelProbe::finish);
            break;
        case Response::Retry:
            start_probing();
            break;
        default:
            BUDDY_UNREACHABLE();
            break;
        }
    }

    void error() {
        switch (wait_for_response(curr_phase)) {
        case Response::Retry:
            start_probing();
            break;
        case Response::Abort:
            fsm_change(PhaseBedLevelProbe::finish);
            break;
        default:
            BUDDY_UNREACHABLE();
            break;
        }
    }

    PhaseBedLevelProbe curr_phase = PhaseBedLevelProbe::intro;
    marlin_server::FSM_Holder holder { PhaseBedLevelProbe::intro };
};

} // namespace

void run(Mode mode) {
    probe_data = {};
    probe_data.mode = mode;
    if (mode == Mode::grid) {
        setup_grid_points();
    } else {
        setup_shim_points();
    }

    BedLevelProbe probe;
    probe.run();
}

} // namespace bed_level_probe

#endif // HAS_BED_LEVEL_PROBE()
