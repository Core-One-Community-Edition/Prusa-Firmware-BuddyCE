#include <menu_bridge.hpp>

#include <ScreenHandler.hpp>
#include <screen.hpp>
#include <i_window_menu.hpp>
#include <i_window_menu_item.hpp>
#include <WindowMenuSpin.hpp>
#include <WindowMenuSwitch.hpp>
#include <WindowMenuItems.hpp>
#include <WindowMenuInfo.hpp>
#include <numeric_input_config.hpp>
#include <timing.h>
#include <freertos/mutex.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace menu_bridge {
namespace {

    using ItemType = IWindowMenuItem::MenuItemType;

    constexpr size_t snapshot_capacity = 6144;
    constexpr uint32_t update_period_ms = 200;

    freertos::Mutex &mutex() {
        static freertos::Mutex instance;
        return instance;
    }

    char snapshot[snapshot_capacity] = "{\"open\":false}";
    size_t snapshot_len = sizeof("{\"open\":false}") - 1;
    uint32_t last_update_ms = 0;
    bool dirty = true;
    int readers = 0;

    struct PendingAction {
        bool present = false;
        ActionOp op = ActionOp::click;
        int index = 0;
        float number = 0;
        bool flag = false;
    };
    PendingAction pending;

    /// Minimal bounded JSON writer into a fixed buffer.
    struct JsonBuf {
        char *p;
        size_t cap;
        size_t len = 0;

        void ch(char c) {
            if (len + 1 < cap) {
                p[len++] = c;
            }
        }
        void raw(const char *s) {
            while (*s) {
                ch(*s++);
            }
        }
        void fmt(const char *format, ...) __attribute__((format(printf, 2, 3))) {
            if (len + 1 >= cap) {
                return;
            }
            va_list args;
            va_start(args, format);
            const int n = vsnprintf(p + len, cap - len, format, args);
            va_end(args);
            if (n > 0) {
                const size_t written = static_cast<size_t>(n);
                len += (written < cap - len) ? written : (cap - len - 1);
            }
        }
        void json_string(const char *s) {
            ch('"');
            for (; *s; ++s) {
                const char c = *s;
                switch (c) {
                case '"':
                    raw("\\\"");
                    break;
                case '\\':
                    raw("\\\\");
                    break;
                case '\n':
                    raw("\\n");
                    break;
                case '\r':
                    raw("\\r");
                    break;
                case '\t':
                    raw("\\t");
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        fmt("\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    } else {
                        ch(c);
                    }
                }
            }
            ch('"');
        }
        void json_sv(const string_view_utf8 &sv) {
            char tmp[128];
            const size_t n = sv.copyToRAM(tmp, sizeof(tmp));
            tmp[(n < sizeof(tmp)) ? n : (sizeof(tmp) - 1)] = '\0';
            json_string(tmp);
        }
    };

    const char *type_name(IWindowMenuItem &item) {
        if (item.has_return_behavior()) {
            return "back";
        }
        switch (item.menu_item_type()) {
        case ItemType::info:
            return "info";
        case ItemType::toggle:
            return "toggle";
        case ItemType::options:
            return "options";
        case ItemType::number:
            return "number";
        case ItemType::submenu:
            return "submenu";
        default:
            return "action";
        }
    }

    void serialize_item(JsonBuf &b, int index, IWindowMenuItem &item) {
        b.fmt("{\"index\":%d,\"type\":\"%s\",", index, type_name(item));
        b.raw("\"label\":");
        b.json_sv(item.GetLabel());
        b.fmt(",\"enabled\":%s", item.IsEnabled() ? "true" : "false");

        switch (item.menu_item_type()) {
        case ItemType::toggle: {
            auto &toggle = static_cast<WI_ICON_SWITCH_OFF_ON_t &>(item);
            b.fmt(",\"value\":%s", toggle.value() ? "true" : "false");
            break;
        }
        case ItemType::number: {
            auto &spin = static_cast<WiSpin &>(item);
            const NumericInputConfig &config = spin.config();
            b.fmt(",\"value\":%g,\"min\":%g,\"max\":%g,\"step\":%g,\"decimals\":%u",
                static_cast<double>(spin.value()),
                static_cast<double>(config.min_value),
                static_cast<double>(config.max_value),
                static_cast<double>(config.step),
                static_cast<unsigned>(config.max_decimal_places));
            b.raw(",\"unit\":");
            b.json_sv(config.unit_str());
            break;
        }
        case ItemType::options: {
            auto &options = static_cast<MenuItemSwitch &>(item);
            b.fmt(",\"selected\":%u", static_cast<unsigned>(options.get_index()));
            b.raw(",\"text\":");
            b.json_sv(options.current_item_text());
            b.raw(",\"options\":[");
            for (size_t k = 0; k < options.item_count(); ++k) {
                if (k) {
                    b.ch(',');
                }
                b.json_sv(options.item_text(k));
            }
            b.ch(']');
            break;
        }
        case ItemType::info: {
            auto &info = static_cast<IWiInfo &>(item);
            b.raw(",\"text\":");
            b.json_sv(info.value());
            break;
        }
        default:
            break;
        }
        b.ch('}');
    }

    void serialize_current_menu(JsonBuf &b) {
        screen_t *screen = Screens::Access()->Get();
        IWindowMenu *menu = screen ? screen->get_menu() : nullptr;
        if (!menu) {
            b.raw("{\"open\":false}");
            return;
        }

        b.raw("{\"open\":true,\"title\":");
        b.json_sv(screen->get_menu_title());
        b.raw(",\"items\":[");

        const int count = menu->item_count();
        bool first = true;
        for (int i = 0; i < count; ++i) {
            IWindowMenuItem *item = menu->item_at(i);
            if (!item) {
                continue;
            }
            if (!first) {
                b.ch(',');
            }
            first = false;
            serialize_item(b, i, *item);
        }
        b.raw("]}");
    }

    void apply(const PendingAction &action) {
        if (action.op == ActionOp::back) {
            Screens::Access()->Close();
            return;
        }

        screen_t *screen = Screens::Access()->Get();
        IWindowMenu *menu = screen ? screen->get_menu() : nullptr;
        if (!menu) {
            return;
        }
        IWindowMenuItem *item = menu->item_at(action.index);
        if (!item || !item->IsEnabled()) {
            return;
        }

        switch (action.op) {
        case ActionOp::click:
            item->Click(*menu);
            break;
        case ActionOp::set_toggle:
            if (item->menu_item_type() == ItemType::toggle) {
                auto &toggle = static_cast<WI_ICON_SWITCH_OFF_ON_t &>(*item);
                if (toggle.value() != action.flag) {
                    item->Click(*menu); // Click toggles and fires OnChange.
                }
            }
            break;
        case ActionOp::set_number:
            if (item->menu_item_type() == ItemType::number) {
                auto &spin = static_cast<WiSpin &>(*item);
                if (!spin.is_edited()) {
                    item->Click(*menu); // Enter edit mode (also moves focus here).
                }
                spin.set_value(action.number);
                if (spin.is_edited()) {
                    item->Click(*menu); // Exit edit mode -> OnClick() commits the value.
                }
            }
            break;
        case ActionOp::back:
            break;
        }
    }

} // namespace

void process() {
    PendingAction local;
    bool have_action = false;
    {
        std::unique_lock lock { mutex() };
        if (pending.present) {
            local = pending;
            pending.present = false;
            have_action = true;
        }
    }

    if (have_action) {
        apply(local);
        dirty = true;
    }

    const uint32_t now = ticks_ms();
    if (!dirty && (now - last_update_ms) < update_period_ms) {
        return;
    }

    std::unique_lock lock { mutex() };
    if (readers > 0) {
        // A client is reading the snapshot; don't overwrite it. Retry next loop.
        return;
    }
    JsonBuf b { snapshot, snapshot_capacity };
    serialize_current_menu(b);
    snapshot[(b.len < snapshot_capacity) ? b.len : (snapshot_capacity - 1)] = '\0';
    snapshot_len = b.len;
    last_update_ms = now;
    dirty = false;
}

const char *acquire_snapshot(size_t &length_out) {
    std::unique_lock lock { mutex() };
    ++readers;
    length_out = snapshot_len;
    return snapshot;
}

void release_snapshot() {
    std::unique_lock lock { mutex() };
    if (readers > 0) {
        --readers;
    }
}

bool post_action(ActionOp op, int index, float number, bool flag) {
    std::unique_lock lock { mutex() };
    if (pending.present) {
        return false;
    }
    pending = PendingAction { true, op, index, number, flag };
    return true;
}

} // namespace menu_bridge
