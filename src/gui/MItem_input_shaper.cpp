#include "MItem_input_shaper.hpp"

#include "ScreenHandler.hpp"
#include <MItem_tools.hpp>
#include <window_msgbox.hpp>
#include <common/utils/algorithm_extensions.hpp>

static constexpr std::array<const char *, 2> is_type_names {
    N_("X-axis Filter"),
    N_("Y-axis Filter"),
};

static constexpr std::array<const char *, 2> cascade_enable_names {
    N_("X-axis 2nd Shaper"),
    N_("Y-axis 2nd Shaper"),
};

static constexpr std::array<const char *, 2> cascade_type_names {
    N_("X-axis 2nd Filter"),
    N_("Y-axis 2nd Filter"),
};

static constexpr std::array<const char *, 2> cascade_freq_names {
    N_("X 2nd Freq."),
    N_("Y 2nd Freq."),
};

MI_IS_TYPE::MI_IS_TYPE(AxisEnum axis)
    : MenuItemSelectMenu(_(is_type_names[axis]))
    , axis_(axis) //
{
    update();
}

void MI_IS_TYPE::update() {
    const auto type = config_store().get_input_shaper_axis_config(axis_).type;
    set_current_item(stdext::index_of(input_shaper::filter_list, type));
}

int MI_IS_TYPE::item_count() const {
    return input_shaper::filter_list.size();
}

void MI_IS_TYPE::build_item_text(int index, const std::span<char> &buffer) const {
    _(input_shaper::filter_names[input_shaper::filter_list[index]]).copyToRAM(buffer);
}

bool MI_IS_TYPE::on_item_selected([[maybe_unused]] int old_index, int new_index) {
    auto config = config_store().get_input_shaper_axis_config(axis_);
    config.type = input_shaper::filter_list[new_index];
    config_store().set_input_shaper_axis_config(axis_, config);

    // Make the input shaper reload config from config_store
    gui_try_gcode_with_msg("M9200");

    return true;
}

static constexpr NumericInputConfig is_frequency_spin_config {
    .min_value = input_shaper::frequency_safe_min,
    .max_value = input_shaper::frequency_safe_max,
    .unit = Unit::hertz,
};

MI_IS_X_FREQUENCY::MI_IS_X_FREQUENCY()
    : WiSpin(0 /* set in ScreenMenuInputShaper::update_gui*/, is_frequency_spin_config, _(label), nullptr, is_enabled_t::no, is_hidden_t::no) {
}

void MI_IS_X_FREQUENCY::OnClick() {
    auto axis_x = config_store().input_shaper_axis_x_config.get();
    axis_x.frequency = static_cast<float>(GetVal());
    config_store().input_shaper_axis_x_config.set(axis_x);

    // Make the input shaper reload config from config_store
    gui_try_gcode_with_msg("M9200");
}

MI_IS_Y_FREQUENCY::MI_IS_Y_FREQUENCY()
    : WiSpin(0 /* set in ScreenMenuInputShaper::update_gui*/, is_frequency_spin_config, _(label), nullptr, is_enabled_t::no, is_hidden_t::no) {
}

void MI_IS_Y_FREQUENCY::OnClick() {
    auto axis_y = config_store().input_shaper_axis_y_config.get();
    axis_y.frequency = static_cast<float>(GetVal());
    config_store().input_shaper_axis_y_config.set(axis_y);

    // Make the input shaper reload config from config_store
    gui_try_gcode_with_msg("M9200");
}

// -----------------------------------------------------------------------
// Cascade (second order) shaper items
// -----------------------------------------------------------------------

MI_IS_CASCADE_ENABLE::MI_IS_CASCADE_ENABLE(AxisEnum axis)
    : MenuItemSwitch(_(cascade_enable_names[axis]), std::span<const char *const, 2>(labels), 0)
    , axis_(axis) //
{
    update();
}

void MI_IS_CASCADE_ENABLE::OnChange(size_t) {
    switch (axis_) {
    case X_AXIS:
        config_store().input_shaper_axis_x_cascade_enabled.set(get_index() == 1);
        break;
    case Y_AXIS:
        config_store().input_shaper_axis_y_cascade_enabled.set(get_index() == 1);
        break;
    default:
        break;
    }

    // Make the input shaper reload config from config_store
    gui_try_gcode_with_msg("M9200");

    // Trigger show/hide of cascade type+frequency items
    Screens::Access()->WindowEvent(GUI_event_t::CHILD_CLICK, std::bit_cast<void *>(InputShaperMenuItemChildClickParam::request_gui_update));
}

void MI_IS_CASCADE_ENABLE::update() {
    switch (axis_) {
    case X_AXIS:
        set_index(config_store().input_shaper_axis_x_cascade_enabled.get() ? 1 : 0);
        break;
    case Y_AXIS:
        set_index(config_store().input_shaper_axis_y_cascade_enabled.get() ? 1 : 0);
        break;
    default:
        break;
    }
}

MI_IS_CASCADE_TYPE::MI_IS_CASCADE_TYPE(AxisEnum axis)
    : MenuItemSelectMenu(_(cascade_type_names[axis]))
    , axis_(axis) //
{
    update();
}

void MI_IS_CASCADE_TYPE::update() {
    input_shaper::Type type = input_shaper::Type::null;
    switch (axis_) {
    case X_AXIS:
        if (config_store().input_shaper_axis_x_cascade_enabled.get()) {
            type = config_store().input_shaper_axis_x_cascade_config.get().type;
        }
        break;
    case Y_AXIS:
        if (config_store().input_shaper_axis_y_cascade_enabled.get()) {
            type = config_store().input_shaper_axis_y_cascade_config.get().type;
        }
        break;
    default:
        break;
    }
    set_current_item(stdext::index_of(input_shaper::filter_list, type));
}

int MI_IS_CASCADE_TYPE::item_count() const {
    return input_shaper::filter_list.size();
}

void MI_IS_CASCADE_TYPE::build_item_text(int index, const std::span<char> &buffer) const {
    _(input_shaper::filter_names[input_shaper::filter_list[index]]).copyToRAM(buffer);
}

bool MI_IS_CASCADE_TYPE::on_item_selected([[maybe_unused]] int old_index, int new_index) {
    switch (axis_) {
    case X_AXIS: {
        auto config = config_store().input_shaper_axis_x_cascade_config.get();
        config.type = input_shaper::filter_list[new_index];
        config_store().input_shaper_axis_x_cascade_config.set(config);
        break;
    }
    case Y_AXIS: {
        auto config = config_store().input_shaper_axis_y_cascade_config.get();
        config.type = input_shaper::filter_list[new_index];
        config_store().input_shaper_axis_y_cascade_config.set(config);
        break;
    }
    default:
        break;
    }

    // Make the input shaper reload config from config_store
    gui_try_gcode_with_msg("M9200");

    return true;
}

MI_IS_CASCADE_FREQUENCY::MI_IS_CASCADE_FREQUENCY(AxisEnum axis)
    : WiSpin(0 /* set in ScreenMenuInputShaper::update_gui*/, cascade_frequency_spin_config, _(cascade_freq_names[axis]), nullptr, is_enabled_t::no, is_hidden_t::no)
    , axis_(axis) //
{
}

void MI_IS_CASCADE_FREQUENCY::OnClick() {
    switch (axis_) {
    case X_AXIS: {
        auto config = config_store().input_shaper_axis_x_cascade_config.get();
        config.frequency = static_cast<float>(GetVal());
        config_store().input_shaper_axis_x_cascade_config.set(config);
        break;
    }
    case Y_AXIS: {
        auto config = config_store().input_shaper_axis_y_cascade_config.get();
        config.frequency = static_cast<float>(GetVal());
        config_store().input_shaper_axis_y_cascade_config.set(config);
        break;
    }
    default:
        break;
    }

    // Make the input shaper reload config from config_store
    gui_try_gcode_with_msg("M9200");
}

#if HAS_INPUT_SHAPER_CALIBRATION()
MI_IS_CALIB::MI_IS_CALIB()
    : IWindowMenuItem(_(label), nullptr, is_enabled_t::yes, marlin_client::is_printing() ? is_hidden_t::yes : is_hidden_t::no) {
}

void MI_IS_CALIB::click([[maybe_unused]] IWindowMenu &window_menu) {
    marlin_client::gcode("M1959");
}
#endif

MI_IS_RESTORE_DEFAULTS::MI_IS_RESTORE_DEFAULTS()
    : IWindowMenuItem(_(label), nullptr) {
}

void MI_IS_RESTORE_DEFAULTS::click([[maybe_unused]] IWindowMenu &window_menu) {
    if (MsgBoxQuestion(_("Do you really want to restore default input shaper configuration?"), Responses_YesNo) != Response::Yes) {
        return;
    }

    // Restore defaults in the config store
    {
        auto &store = config_store();
        auto transaction = store.get_backend().transaction_guard();
        store.input_shaper_axis_x_config.set_to_default();
        store.input_shaper_axis_y_config.set_to_default();
        store.input_shaper_weight_adjust_y_config.set_to_default();
        store.input_shaper_axis_x_enabled.set_to_default();
        store.input_shaper_axis_y_enabled.set_to_default();
        store.input_shaper_weight_adjust_y_enabled.set_to_default();
        store.input_shaper_axis_x_cascade_enabled.set_to_default();
        store.input_shaper_axis_x_cascade_config.set_to_default();
        store.input_shaper_axis_y_cascade_enabled.set_to_default();
        store.input_shaper_axis_y_cascade_config.set_to_default();
    }

    // Make the input shaper reload config from config_store
    gui_try_gcode_with_msg("M9200");

    Screens::Access()->WindowEvent(GUI_event_t::CHILD_CLICK, std::bit_cast<void *>(InputShaperMenuItemChildClickParam::request_gui_update));
}
