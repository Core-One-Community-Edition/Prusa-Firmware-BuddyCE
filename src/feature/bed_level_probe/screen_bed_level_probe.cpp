#include <feature/bed_level_probe/screen_bed_level_probe.hpp>

#if HAS_BED_LEVEL_PROBE()

#include <feature/bed_level_probe/bed_level_probe.hpp>
#include <i18n.h>
#include <window_text.hpp>
#include <guiconfig/GuiDefaults.hpp>
#include <radio_button_fsm.hpp>
#include <auto_layout.hpp>
#include <display.hpp>
#include <display_helper.h>
#include <fonts.hpp>
#include <utils/color.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

static_assert(HAS_BED_LEVEL_PROBE(), "Doesn't support bed level probe");

namespace {

constexpr auto txt_intro_shims = N_("This measures the three bed mounting points and recommends shims to level the bed.\n\nThe gantry is aligned, the printer homes, then each mount is probed. Make sure the bed and nozzle are clean and the temperature is stable.");
constexpr auto txt_intro_grid = N_("This probes the whole bed and shows the distance from the nozzle at each point, helping you find high and low spots.\n\nThe printer homes, then probes the bed. Make sure the bed and nozzle are clean and the temperature is stable.");
constexpr auto txt_homing = N_("Homing...");
constexpr auto txt_error = N_("Probing failed. Check that the load cell is working and the bed and nozzle are clean, then retry.");

constexpr Font map_font = Font::small;
constexpr int16_t line_h = height(map_font);

// Colour ramp: low magnitude is a calm light grey, high magnitude is a bright
// red, so the further a point is from the nozzle the more it stands out.
Color magnitude_color(long dist_um, long range_um) {
    const float t = std::clamp(static_cast<float>(dist_um) / static_cast<float>(range_um), 0.f, 1.f);
    const auto lerp = [t](int a, int b) {
        return static_cast<uint8_t>(a + (b - a) * t);
    };
    return Color::from_rgb(lerp(0xC0, 0xFF), lerp(0xC0, 0x30), lerp(0xC0, 0x30));
}

// Custom window that draws the probe results as a colour-coded map: the grid for
// the full-bed probe, or the three mount points laid out spatially for shims.
class WindowProbeMap : public window_t {
public:
    WindowProbeMap(window_t *parent, Rect16 rect)
        : window_t(parent, rect) {}

protected:
    void unconditionalDraw() override {
        const auto &d = bed_level_probe::probe_data;
        const Rect16 r = GetRect();
        const Color bg = GetParent() ? GetParent()->GetBackColor() : GetBackColor();
        display::fill_rect(r, bg);

        const float max_z = bed_level_probe::reference_z();
        const long range_um = compute_range_um(max_z);

        const int16_t left = r.Left();
        const int16_t width = r.Width();

        // Subtitle describing what the numbers mean.
        const auto subtitle = (d.mode == bed_level_probe::Mode::grid)
            ? _("Distance to nozzle [um], 0 = closest")
            : _("Distance to nozzle [mm], 0 = closest");
        draw_label(Rect16(left, r.Top(), width, line_h), subtitle);

        // Leave a margin below the subtitle and above the radio buttons so the
        // measured values are not crowded against them.
        const int16_t content_top = r.Top() + 2 * line_h;
        const int16_t content_bottom = r.Top() + r.Height() - line_h;

        if (d.mode == bed_level_probe::Mode::grid) {
            draw_grid(left, width, content_top, content_bottom, max_z, range_um);
        } else {
            draw_shims(left, width, content_top, content_bottom, max_z, range_um);
        }
    }

private:
    static long compute_range_um(float max_z) {
        const auto &d = bed_level_probe::probe_data;
        float min_z = NAN;
        for (uint8_t i = 0; i < d.count; ++i) {
            const float z = d.points[i].z;
            if (!isnan(z) && (isnan(min_z) || z < min_z)) {
                min_z = z;
            }
        }
        if (isnan(max_z) || isnan(min_z)) {
            return 150;
        }
        // Floor the range so a nearly flat bed stays in the calm colour range.
        return std::max(lroundf((max_z - min_z) * 1000.f), 150L);
    }

    void draw_label(Rect16 rect, const string_view_utf8 &text) {
        const Color bg = GetParent() ? GetParent()->GetBackColor() : GetBackColor();
        render_text_align(rect, text, map_font, bg, COLOR_WHITE, {}, Align_t::Center());
    }

    // Draws the distance value. The grid uses micrometres; the shim points use
    // millimetres rounded to 0.1 mm, the finest practical shim thickness.
    void draw_value(Rect16 rect, float z, float max_z, long range_um, bool millimeters = false) {
        const Color bg = GetParent() ? GetParent()->GetBackColor() : GetBackColor();
        char buf[8];
        Color fg;
        if (isnan(z) || isnan(max_z)) {
            snprintf(buf, sizeof(buf), "...");
            fg = COLOR_DARK_GRAY;
        } else {
            const long dist = lroundf((max_z - z) * 1000.f);
            if (millimeters) {
                snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(max_z - z));
            } else {
                snprintf(buf, sizeof(buf), "%ld", dist);
            }
            fg = magnitude_color(dist, range_um);
        }
        render_text_align(rect, string_view_utf8::MakeRAM(buf), map_font, bg, fg, {}, Align_t::Center());
    }

    void draw_grid(int16_t left, int16_t width, int16_t top, int16_t bottom, float max_z, long range_um) {
        const auto &d = bed_level_probe::probe_data;
        const int16_t section_gap = line_h / 2;

        // "back" label, then the rows, then "front" label, with generous gaps.
        draw_label(Rect16(left, top, width, line_h), _("back"));
        const int16_t rows_top = top + line_h + section_gap;
        const int16_t rows_bottom = bottom - line_h - section_gap;
        const int16_t row_step = (rows_bottom - rows_top) / d.rows;
        const int16_t col_step = width / d.cols;

        for (uint8_t i = 0; i < d.rows; ++i) {
            const uint8_t row = d.rows - 1 - i; // Back (highest Y) on top.
            const int16_t y = rows_top + i * row_step;
            for (uint8_t col = 0; col < d.cols; ++col) {
                const int16_t x = left + col * col_step;
                draw_value(Rect16(x, y, col_step, line_h), d.points[row * d.cols + col].z, max_z, range_um);
            }
        }

        draw_label(Rect16(left, static_cast<int16_t>(bottom - line_h), width, line_h), _("front"));
    }

    // Draws a labelled point (name above, coloured value below).
    void draw_point(int16_t x, int16_t y, int16_t w, const string_view_utf8 &label, float z, float max_z, long range_um) {
        draw_label(Rect16(x, y, w, line_h), label);
        draw_value(Rect16(x, static_cast<int16_t>(y + line_h), w, line_h), z, max_z, range_um, /*millimeters=*/true);
    }

    void draw_shims(int16_t left, int16_t width, int16_t top, int16_t bottom, float max_z, long range_um) {
        const auto &d = bed_level_probe::probe_data;
        const int16_t third = width / 3;
        const int16_t point_h = 2 * line_h; // Label + value.

        // Three mounts laid out as the physical triangle: the back mount at the
        // top center, the two front mounts at the bottom corners.
        const int16_t apex_y = top;
        const int16_t base_y = bottom - point_h;

        draw_point(static_cast<int16_t>(left + third), apex_y, third, _("Back"), d.points[2].z, max_z, range_um);
        draw_point(left, base_y, third, _("Left front"), d.points[0].z, max_z, range_um);
        draw_point(static_cast<int16_t>(left + 2 * third), base_y, third, _("Right front"), d.points[1].z, max_z, range_um);
    }
};

// Frame shared by the live probing phase and the final results: a colour-coded
// map plus the FSM radio buttons.
class FrameProbeResults final {
public:
    FrameProbeResults(window_frame_t *parent, FSMAndPhase fsm_phase)
        : map(parent, {})
        , radio(parent, {}, fsm_phase) {

        parent->CaptureNormalWindow(radio);

        std::array<window_t *, 2> windows { &map, &radio };
        layout_vertical_stack(parent->GetRect(), windows, layout);
    }

    void update(fsm::PhaseData) {
        map.Invalidate();
    }

private:
    static constexpr std::array layout {
        StackLayoutItem { .height = StackLayoutItem::stretch, .margin_side = 8, .margin_top = 8 },
        standard_stack_layout::for_radio,
    };

    WindowProbeMap map;
    RadioButtonFSM radio;
};

// Intro frame with text tailored to the selected workflow.
class FrameIntro final {
public:
    FrameIntro(window_frame_t *parent, FSMAndPhase fsm_phase)
        : info(parent, {}, is_multiline::yes, is_closed_on_click_t::no,
              _(bed_level_probe::probe_data.mode == bed_level_probe::Mode::shims ? txt_intro_shims : txt_intro_grid))
        , radio(parent, {}, fsm_phase) {

        info.SetAlignment(Align_t::Center());
        parent->CaptureNormalWindow(radio);

        std::array<window_t *, 2> windows { &info, &radio };
        layout_vertical_stack(parent->GetRect(), windows, layout);
    }

private:
    static constexpr std::array layout {
        StackLayoutItem { .height = StackLayoutItem::stretch, .margin_side = 16, .margin_top = 16 },
        standard_stack_layout::for_radio,
    };

    window_text_t info;
    RadioButtonFSM radio;
};

// Plain status text with FSM radio, used for the homing and error phases.
class FrameStatus final {
public:
    FrameStatus(window_frame_t *parent, FSMAndPhase fsm_phase, const string_view_utf8 &txt)
        : info(parent, {}, is_multiline::yes, is_closed_on_click_t::no, txt)
        , radio(parent, {}, fsm_phase) {

        info.SetAlignment(Align_t::Center());
        parent->CaptureNormalWindow(radio);

        std::array<window_t *, 2> windows { &info, &radio };
        layout_vertical_stack(parent->GetRect(), windows, layout);
    }

private:
    static constexpr std::array layout {
        StackLayoutItem { .height = StackLayoutItem::stretch, .margin_side = 16, .margin_top = 16 },
        standard_stack_layout::for_radio,
    };

    window_text_t info;
    RadioButtonFSM radio;
};

using Frames = FrameDefinitionList<ScreenBedLevelProbe::FrameStorage,
    FrameDefinition<PhaseBedLevelProbe::intro, FrameIntro, PhaseBedLevelProbe::intro>,
    FrameDefinition<PhaseBedLevelProbe::homing, FrameStatus, PhaseBedLevelProbe::homing, txt_homing>,
    FrameDefinition<PhaseBedLevelProbe::probing, FrameProbeResults, PhaseBedLevelProbe::probing>,
    FrameDefinition<PhaseBedLevelProbe::results, FrameProbeResults, PhaseBedLevelProbe::results>,
    FrameDefinition<PhaseBedLevelProbe::error, FrameStatus, PhaseBedLevelProbe::error, txt_error>>;

} // namespace

ScreenBedLevelProbe::ScreenBedLevelProbe()
    : ScreenFSM {
        bed_level_probe::probe_data.mode == bed_level_probe::Mode::shims ? N_("BED LEVEL SHIMS") : N_("BED FLATNESS MAP"),
        GuiDefaults::RectScreenNoHeader,
    } {
    CaptureNormalWindow(inner_frame);
    create_frame();
}

ScreenBedLevelProbe::~ScreenBedLevelProbe() {
    destroy_frame();
}

void ScreenBedLevelProbe::create_frame() {
    Frames::create_frame(frame_storage, get_phase(), &inner_frame);
}

void ScreenBedLevelProbe::destroy_frame() {
    Frames::destroy_frame(frame_storage, get_phase());
}

void ScreenBedLevelProbe::update_frame() {
    Frames::update_frame(frame_storage, get_phase(), fsm_base_data.GetData());
}

#endif // HAS_BED_LEVEL_PROBE()
