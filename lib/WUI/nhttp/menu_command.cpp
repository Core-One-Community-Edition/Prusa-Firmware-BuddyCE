#include "menu_command.hpp"
#include "handler.h"
#include "json_parser.h"

#include <menu_bridge.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace nhttp::printer {

using namespace handler;
using http::Status;
using json::Event;
using std::nullopt;
using std::string_view;

namespace {

    // Copies a (non null-terminated) string_view value into a small temp buffer.
    template <size_t N>
    void copy_value(const string_view &value, char (&dst)[N]) {
        const size_t n = std::min(value.size(), N - 1);
        memcpy(dst, value.data(), n);
        dst[n] = '\0';
    }

} // namespace

MenuCommand::MenuCommand(size_t content_length, bool can_keep_alive, bool json_errors)
    : content_length(content_length)
    , can_keep_alive(can_keep_alive)
    , json_errors(json_errors) {
    memset(buffer.data(), 0, buffer.size());
}

void MenuCommand::step(std::string_view input, bool terminated_by_client, uint8_t *, size_t, Step &out) {
    if (content_length > buffer.size()) {
        out = Step { 0, 0, StatusPage(Status::PayloadTooLarge, StatusPage::CloseHandling::ErrorClose, json_errors) };
        return;
    }

    const size_t rest = content_length - buffer_used;
    const size_t to_read = std::min(input.size(), rest);

    memcpy(buffer.data() + buffer_used, input.data(), to_read);
    buffer_used += to_read;

    if (content_length > buffer_used) {
        if (terminated_by_client) {
            out = Step { to_read, 0, StatusPage(Status::BadRequest, StatusPage::CloseHandling::ErrorClose, json_errors, nullopt, "Truncated request") };
            return;
        } else {
            out = Step { to_read, 0, Continue() };
            return;
        }
    }

    out = Step { to_read, 0, process() };
}

StatusPage MenuCommand::process() {
    const auto close_handling = can_keep_alive ? StatusPage::CloseHandling::KeepAlive : StatusPage::CloseHandling::Close;

    char op[12] = "";
    int index = 0;
    float number = 0;
    bool flag = false;
    bool has_index = false;
    bool has_number = false;
    bool has_bool = false;

    const auto parse_result = parse_command(reinterpret_cast<char *>(buffer.data()), buffer_used, [&](const Event &event) {
        if (event.depth != 1 || !event.key.has_value() || !event.value.has_value()) {
            return;
        }
        const auto &key = event.key.value();
        const auto &value = event.value.value();

        if (key == "op") {
            copy_value(value, op);
        } else if (key == "index") {
            char tmp[16];
            copy_value(value, tmp);
            index = atoi(tmp);
            has_index = true;
        } else if (key == "number") {
            char tmp[32];
            copy_value(value, tmp);
            number = strtof(tmp, nullptr);
            has_number = true;
        } else if (key == "bool") {
            if (value == "true") {
                flag = true;
                has_bool = true;
            } else if (value == "false") {
                flag = false;
                has_bool = true;
            }
        }
    });

    switch (parse_result) {
    case JsonParseResult::ErrMem:
        return StatusPage(Status::PayloadTooLarge, close_handling, json_errors, nullopt, "Too many JSON tokens");
    case JsonParseResult::ErrReq:
        return StatusPage(Status::BadRequest, close_handling, json_errors, nullopt, "Couldn't parse JSON");
    case JsonParseResult::Ok:
        break;
    }

    bool queued = false;
    if (strcmp(op, "back") == 0) {
        queued = menu_bridge::post_action(menu_bridge::ActionOp::back, 0, 0, false);
    } else if (strcmp(op, "click") == 0 && has_index) {
        queued = menu_bridge::post_action(menu_bridge::ActionOp::click, index, 0, false);
    } else if (strcmp(op, "set") == 0 && has_index && has_number) {
        queued = menu_bridge::post_action(menu_bridge::ActionOp::set_number, index, number, false);
    } else if (strcmp(op, "set") == 0 && has_index && has_bool) {
        queued = menu_bridge::post_action(menu_bridge::ActionOp::set_toggle, index, 0, flag);
    } else {
        return StatusPage(Status::BadRequest, close_handling, json_errors, nullopt, "Invalid menu command");
    }

    if (queued) {
        return StatusPage(Status::NoContent, close_handling, json_errors);
    }
    return StatusPage(Status::Conflict, close_handling, json_errors, nullopt, "Menu busy");
}

} // namespace nhttp::printer
