#include "screen_motor_vibration.hpp"
#include <fsm/motor_vibration_phases.hpp>
#include <img_resources.hpp>
#include <guiconfig/wizard_config.hpp>
#include <window_numb.hpp>
#include <client_response.hpp>
#include <dialogs/radio_button.hpp>
#include <marlin_client.hpp>
#include <meta_utils.hpp>

using Phase = PhaseMotorVibration;

namespace {

// Introduction PHASE: intro
constexpr const char *txt_title_intro = N_("Motor Vibration Tool");
constexpr const char *txt_desc_intro = N_("Vibrate a motor at adjustable frequency to find resonating parts. Use the knob to adjust frequency while watching for resonances.");

// Motor selection PHASE: select_motor
constexpr const char *txt_title_select = N_("Select Motor");
constexpr const char *txt_desc_select = N_("Choose which motor to vibrate.\n\nMotor A drives the X-axis.\nMotor B drives the Y-axis.");

// Vibration PHASE: vibrate
constexpr const char *txt_title_vibrate = N_("Motor Vibration");
constexpr const char *txt_desc_vibrate = N_("Turn the knob to adjust frequency. Look for slow belt movement with sharp, regular peaks, then click Done to stop.");

// Finish screen
constexpr const char *txt_title_finished = N_("Vibration Complete");
constexpr const char *txt_desc_finished = N_("Motor vibration has finished.\nYou're all set.\n\nPress Finish to exit.");

constexpr uint8_t qr_size = 100;

constexpr Rect16 rect_title = Rect16(WizardDefaults::MarginLeft, WizardDefaults::row_0, GuiDefaults::ScreenWidth - WizardDefaults::MarginLeft - WizardDefaults::MarginRight, WizardDefaults::txt_h);
constexpr Rect16 rect_line = Rect16(WizardDefaults::MarginLeft, WizardDefaults::row_1, GuiDefaults::ScreenWidth - WizardDefaults::MarginLeft - WizardDefaults::MarginRight, 1);

constexpr Rect16 rect_desc = Rect16(WizardDefaults::MarginLeft, WizardDefaults::row_1 + 10, GuiDefaults::ScreenWidth - WizardDefaults::MarginLeft - WizardDefaults::MarginRight, WizardDefaults::Y_space - WizardDefaults::RectRadioButton(0).Height() - WizardDefaults::row_h - 30);
constexpr Rect16 rect_desc_knob = Rect16(WizardDefaults::MarginLeft, WizardDefaults::row_1 + 10, GuiDefaults::ScreenWidth - WizardDefaults::MarginLeft - WizardDefaults::MarginRight, WizardDefaults::Y_space - WizardDefaults::RectRadioButton(0).Height() - WizardDefaults::row_h - 80);

constexpr Rect16 rect_numb = Rect16(GuiDefaults::ScreenWidth / 2 - 50, WizardDefaults::RectRadioButton(0).Top() - 100, 100, 22);
constexpr Rect16 rect_knob = Rect16(GuiDefaults::ScreenWidth / 2 - 41, WizardDefaults::RectRadioButton(0).Top() - 70, 81, 55);
constexpr Rect16 rect_minus = Rect16(GuiDefaults::ScreenWidth / 2 - 61, WizardDefaults::RectRadioButton(0).Top() - 70, 20, 55);
constexpr Rect16 rect_plus = Rect16(GuiDefaults::ScreenWidth / 2 + 40, WizardDefaults::RectRadioButton(0).Top() - 70, 20, 55);

constexpr Rect16 rect_text_joe = Rect16(WizardDefaults::MarginLeft, WizardDefaults::row_1 + 10, GuiDefaults::ScreenWidth - WizardDefaults::MarginLeft - WizardDefaults::MarginRight - 50, WizardDefaults::Y_space - WizardDefaults::RectRadioButton(0).Height() - WizardDefaults::row_h - 30 - 64);
constexpr Rect16 rect_joe = Rect16(0, rect_text_joe.Bottom(), GuiDefaults::ScreenWidth, 64);

} // namespace

namespace frames {

class MVFrameTitle {
public:
    MVFrameTitle(window_frame_t *parent, const char *txt_title)
        : line(parent, rect_line)
        , title(parent, rect_title, is_multiline::no, is_closed_on_click_t::no, _(txt_title)) {
        line.SetBackColor(COLOR_WHITE);
        title.set_font(Font::big);
    }

protected:
    BasicWindow line;
    window_text_t title;
};



class MVFrameTitleRadio : public MVFrameTitle {

public:
    MVFrameTitleRadio(window_frame_t *parent, Phase phase, const char *txt_title)
        : MVFrameTitle(parent, txt_title)
        , radio(parent, WizardDefaults::RectRadioButton(0), phase) {
        parent->CaptureNormalWindow(radio);
    }

protected:
    RadioButtonFSM radio;
};

class MVFrameTitleDescRadio : public MVFrameTitleRadio {

public:
    MVFrameTitleDescRadio(window_frame_t *parent, Phase phase, const char *title, const char *desc)
        : MVFrameTitleRadio(parent, phase, title)
        , desc(parent, rect_desc, is_multiline::yes, is_closed_on_click_t::no, _(desc)) {}

protected:
    window_text_t desc;
};

// Motor selection frame with custom button labels: "Motor A" / "Motor B"
class MVFrameSelectMotor : public MVFrameTitle {
public:
    MVFrameSelectMotor(window_frame_t *parent, Phase phase, const char *title, const char *desc)
        : MVFrameTitle(parent, title)
        , desc(parent, rect_desc, is_multiline::yes, is_closed_on_click_t::no, _(desc))
        , radio(parent, WizardDefaults::RectRadioButton(0), FSMAndPhase(phase),
                IRadioButton::Responses_t { Response::Left, Response::Right, Response::_none, Response::_none },
                &motor_labels) {
        parent->CaptureNormalWindow(radio);
    }

private:
    static constexpr PhaseTexts motor_labels { { N_("Motor A"), N_("Motor B"), "", "" } };
    window_text_t desc;
    RadioButtonFSM radio;
};

// Sweep mode selection frame with custom button labels: "Raw" / "Shaped"
class MVFrameSelectSweepMode : public MVFrameTitle {
public:
    MVFrameSelectSweepMode(window_frame_t *parent, Phase phase, const char *title, const char *desc)
        : MVFrameTitle(parent, title)
        , desc(parent, rect_desc, is_multiline::yes, is_closed_on_click_t::no, _(desc))
        , radio(parent, WizardDefaults::RectRadioButton(0), FSMAndPhase(phase),
                IRadioButton::Responses_t { Response::Left, Response::Right, Response::_none, Response::_none },
                &sweep_labels) {
        parent->CaptureNormalWindow(radio);
    }

private:
    static constexpr PhaseTexts sweep_labels { { N_("Raw"), N_("Shaped"), "", "" } };
    window_text_t desc;
    RadioButtonFSM radio;
};

class MVFrameFinishJoe : public MVFrameTitleRadio {
public:
    MVFrameFinishJoe(window_frame_t *parent, Phase phase, const char *title, const char *desc)
        : MVFrameTitleRadio(parent, phase, title)
        , desc(parent, rect_text_joe, is_multiline::yes, is_closed_on_click_t::no, _(desc))
        , joe(parent, rect_joe, &img::pepa_42x64) {
        joe.SetAlignment(Align_t::Center());
    }

protected:
    window_text_t desc;
    window_icon_t joe;
};

class MVFrameAdjustKnob : public MVFrameTitle {

    class DoneResponder : public window_t {

    public:
        DoneResponder(window_t *parent, Phase phase)
            : window_t(parent, Rect16 {})
            , phase_(phase) {
        }

    protected:
        const Phase phase_;

        virtual void windowEvent([[maybe_unused]] window_t *sender, GUI_event_t event, [[maybe_unused]] void *param) override {
            switch (event) {
            case GUI_event_t::CLICK:
                marlin_client::FSM_response(phase_, Response::Done);
                break;
            default:
                break;
            }
        }
    };

public:
    MVFrameAdjustKnob(window_frame_t *parent, Phase phase, const char *title, const char *desc)
        : MVFrameTitle(parent, title)
        , phase(phase)
        , desc(parent, rect_desc_knob, is_multiline::yes, is_closed_on_click_t::no, _(desc))
        , numb(parent, rect_numb, 0, "%0.1f Hz")
        , knob(parent, rect_knob, &img::turn_knob_81x55)
        , plus(parent, rect_plus, is_multiline::no, is_closed_on_click_t::no, string_view_utf8::MakeRAM("+"))
        , minus(parent, rect_minus, is_multiline::no, is_closed_on_click_t::no, string_view_utf8::MakeRAM("-"))
        , done(parent, phase) {
        plus.SetAlignment(Align_t::Center());
        plus.set_font(Font::big);
        plus.SetTextColor(COLOR_BRAND);
        minus.SetAlignment(Align_t::Center());
        minus.set_font(Font::big);
        minus.SetTextColor(COLOR_BRAND);
        numb.SetAlignment(Align_t::Center());

        static_cast<window_frame_t *>(parent)->CaptureNormalWindow(done);
    }

    void update(fsm::PhaseData data) {
        const auto vib_data = fsm::deserialize_data<vibration_data>(data);
        numb.SetValue(vib_data.get());
    }

private:
    Phase phase;
    window_text_t desc;
    window_numb_t numb;
    window_icon_t knob;
    window_text_t plus;
    window_text_t minus;
    DoneResponder done;
};

} // namespace frames

namespace {

using Frames = FrameDefinitionList<ScreenMotorVibration::FrameStorage,
    FrameDefinition<Phase::intro, frames::MVFrameTitleDescRadio, Phase::intro, txt_title_intro, txt_desc_intro>,
    FrameDefinition<Phase::select_motor, frames::MVFrameTitleDescRadio, Phase::select_motor, txt_title_select, txt_desc_select>,
    FrameDefinition<Phase::vibrate, frames::MVFrameAdjustKnob, Phase::vibrate, txt_title_vibrate, txt_desc_vibrate>,
    FrameDefinition<Phase::finished, frames::MVFrameFinishJoe, Phase::finished, txt_title_finished, txt_desc_finished>>;

} // namespace

ScreenMotorVibration::ScreenMotorVibration()
    : ScreenFSM("MOTOR VIBRATION", GuiDefaults::RectScreenNoHeader) {
    header.SetIcon(&img::selftest_16x16);
    CaptureNormalWindow(inner_frame);
    create_frame();
}

ScreenMotorVibration::~ScreenMotorVibration() {
    destroy_frame();
}

void ScreenMotorVibration::create_frame() {
    Frames::create_frame(frame_storage, get_phase(), &inner_frame);
}

void ScreenMotorVibration::destroy_frame() {
    Frames::destroy_frame(frame_storage, get_phase());
}

void ScreenMotorVibration::update_frame() {
    Frames::update_frame(frame_storage, get_phase(), fsm_base_data.GetData());
}
