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
#include <core/serial.h>
#include <core/utility.h>

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
        if (!probe_data.interactive) {
            start_probing();
            return;
        }
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
        if (!probe_data.interactive) {
            fsm_change(PhaseBedLevelProbe::finish);
            return;
        }
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
        if (!probe_data.interactive) {
            fsm_change(PhaseBedLevelProbe::finish);
            return;
        }
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

void run(Mode mode, bool interactive) {
    probe_data = {};
    probe_data.mode = mode;
    probe_data.interactive = interactive;
    if (mode == Mode::grid) {
        setup_grid_points();
    } else {
        setup_shim_points();
    }

    BedLevelProbe probe;
    probe.run();
}

// ---------------------------------------------------------------------------
// Serial reporting – mirrors the G29 "Bed Topography Report" format.
// ---------------------------------------------------------------------------

namespace {

// Width per cell: space + sign + 3 integer digits + '.' + 3 fractional + space = 9
// (matches the [-3.567] format used by UBL display_map).
constexpr uint16_t eachsp = 1 + 6 + 1;

void serial_echo_xy(uint16_t sp, int16_t x, int16_t y) {
    SERIAL_ECHO_SP(sp);
    SERIAL_CHAR('(');
    if (x < 100) {
        SERIAL_CHAR(' ');
        if (x < 10)
            SERIAL_CHAR(' ');
    }
    SERIAL_ECHO(x);
    SERIAL_CHAR(',');
    if (y < 100) {
        SERIAL_CHAR(' ');
        if (y < 10)
            SERIAL_CHAR(' ');
    }
    SERIAL_ECHO(y);
    SERIAL_CHAR(')');
    serial_delay(5);
}

void serial_echo_column_labels(uint8_t sp) {
    SERIAL_ECHO_SP(7);
    for (uint8_t i = 0; i < probe_data.cols; i++) {
        if (i < 10)
            SERIAL_CHAR(' ');
        SERIAL_ECHO(i);
        SERIAL_ECHO_SP(sp);
    }
    serial_delay(10);
}

void report_grid() {
    const uint16_t twixt = eachsp * probe_data.cols - 9 * 2;

    SERIAL_ECHOLNPGM("\nBed Flatness Report:");
    SERIAL_EOL();

    // Top corner coordinates.
    serial_echo_xy(4, static_cast<int16_t>(grid_min_x), static_cast<int16_t>(grid_max_y));
    serial_echo_xy(twixt, static_cast<int16_t>(grid_max_x), static_cast<int16_t>(grid_max_y));
    SERIAL_EOL();
    serial_echo_column_labels(eachsp - 2);
    SERIAL_EOL();

    // Rows, top (max Y) to bottom (min Y).
    for (int8_t row = static_cast<int8_t>(probe_data.rows) - 1; row >= 0; row--) {
        // Row label.
        if (row < 10)
            SERIAL_CHAR(' ');
        SERIAL_ECHO(row);
        SERIAL_ECHOPGM(" |");

        for (uint8_t col = 0; col < probe_data.cols; col++) {
            SERIAL_CHAR(' ');

            const float f = probe_data.points[row * probe_data.cols + col].z;
            if (isnan(f))
                SERIAL_ECHOPGM("  .   ");
            else {
                if (f >= 0.0f)
                    SERIAL_CHAR(f > 0 ? '+' : ' ');
                SERIAL_ECHO_F(f, 3);
            }

            SERIAL_CHAR(' ');
            SERIAL_FLUSHTX();
            idle(false);
        }

        SERIAL_EOL();
        // Blank line between rows.
        if (row > 0)
            SERIAL_ECHOLNPGM("   |");
    }

    // Bottom corner coordinates.
    serial_echo_column_labels(eachsp - 2);
    SERIAL_EOL();
    serial_echo_xy(4, static_cast<int16_t>(grid_min_x), static_cast<int16_t>(grid_min_y));
    serial_echo_xy(twixt, static_cast<int16_t>(grid_max_x), static_cast<int16_t>(grid_min_y));
    SERIAL_EOL();
    SERIAL_EOL();
}

void report_shims() {
    SERIAL_ECHOLNPGM("\nShim Calibration Report:");
    SERIAL_EOL();

    static constexpr const char *labels[] = { "Left front", "Right front", "Back center" };

    const float ref = reference_z();
    if (isnan(ref)) {
        SERIAL_ECHOLNPGM("  No valid probe data.");
        SERIAL_EOL();
        return;
    }

    SERIAL_ECHOPGM("  Reference Z (closest to nozzle): ");
    SERIAL_ECHO_F(ref, 3);
    SERIAL_EOL();

    for (uint8_t i = 0; i < probe_data.count; i++) {
        const float z = probe_data.points[i].z;
        SERIAL_ECHOPGM("  ");
        SERIAL_ECHO(labels[i]);
        SERIAL_ECHOPGM(": ");
        if (isnan(z))
            SERIAL_ECHOPGM("probe failed");
        else {
            SERIAL_ECHO_F(z, 3);
            SERIAL_ECHOPGM(" mm  (offset: ");
            const float offset = z - ref;
            if (offset >= 0.0f)
                SERIAL_CHAR('+');
            SERIAL_ECHO_F(offset, 3);
            SERIAL_ECHOPGM(" mm)");
        }
        SERIAL_EOL();
        serial_delay(5);
    }
    SERIAL_EOL();
}

} // namespace

void report_serial() {
    if (probe_data.mode == Mode::grid)
        report_grid();
    else
        report_shims();
}

} // namespace bed_level_probe

#endif // HAS_BED_LEVEL_PROBE()
