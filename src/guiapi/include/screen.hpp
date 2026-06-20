/**
 * @file screen.hpp
 * @author Radek Vana
 * @brief Similar to frame but can have dialogs.
 * @date 2020-11-29
 */

#pragma once

#include "window_frame.hpp"
#include "screen_init_variant.hpp"
#include "compact_pointer.hpp"
#include <string_view_utf8.hpp>

class IWindowMenu;

// DO NOT SET has_relative_subwins flag !!!
// screen must have rect == GuiDefaults::RectScreen
class screen_t : public window_frame_t {
    CompactRAMPointer<window_t> first_dialog;
    CompactRAMPointer<window_t> last_dialog;

public:
    screen_t(window_t *parent = nullptr, win_type_t type = win_type_t::normal, is_closed_on_timeout_t timeout = is_closed_on_timeout_t::yes, is_closed_on_printing_t close_on_print = is_closed_on_printing_t::yes);
    ~screen_t();

    /// If this screen is a menu screen, returns its menu (for the remote menu
    /// API), otherwise nullptr. Overridden by ScreenMenuBase_.
    virtual IWindowMenu *get_menu() { return nullptr; }

    /// Title of this screen's menu, or empty if not a menu screen.
    virtual string_view_utf8 get_menu_title() const { return {}; }

    virtual window_t *GetCapturedWindow() override;
    virtual void ChildVisibilityChanged(window_t &child) override;

    window_t *get_child_dialog([[maybe_unused]] ChildDialogParam param) const override;

    /// Restores state from the provided variant
    virtual void InitState([[maybe_unused]] screen_init_variant var) {}
    virtual screen_init_variant GetCurrentState() const { return screen_init_variant(); }

protected:
    virtual bool registerSubWin(window_t &win) override;
    virtual void unregisterSubWin(window_t &win) override;

    void hideSubwinsBehindDialogs();
    window_t *findCaptured_first_last(window_t *first, window_t *last) const; // does not use begin - end like normal find

protected:
    virtual void screenEvent(window_t *sender, GUI_event_t event, void *param) override;
};
