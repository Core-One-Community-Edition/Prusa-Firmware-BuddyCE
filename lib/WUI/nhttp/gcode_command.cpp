#include "gcode_command.h"
#include "handler.h"
#include "json_parser.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace nhttp::printer {

using namespace handler;
using http::Status;
using json::Event;
using json::Type;
using std::nullopt;
using std::string_view;

GcodeCommand::GcodeCommand(size_t content_length, bool can_keep_alive, bool json_errors)
    : content_length(content_length)
    , can_keep_alive(can_keep_alive)
    , json_errors(json_errors) {
    memset(buffer.data(), 0, buffer.size());
}

void GcodeCommand::step(std::string_view input, bool terminated_by_client, uint8_t *, size_t, Step &out) {
    if (content_length > buffer.size()) {
        // Refuse early, without reading the body -> drop the connection too.
        out = Step { 0, 0, StatusPage(Status::PayloadTooLarge, StatusPage::CloseHandling::ErrorClose, json_errors) };
        return;
    }

    const size_t rest = content_length - buffer_used;
    const size_t to_read = std::min(input.size(), rest);

    memcpy(buffer.data() + buffer_used, input.data(), to_read);
    buffer_used += to_read;

    if (content_length > buffer_used) {
        // Still waiting for more data.
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

StatusPage GcodeCommand::process() {
    const auto close_handling = can_keep_alive ? StatusPage::CloseHandling::KeepAlive : StatusPage::CloseHandling::Close;

    char gcode[BUFFER_LEN + 1];
    gcode[0] = '\0';

    const auto parse_result = parse_command(reinterpret_cast<char *>(buffer.data()), buffer_used, [&](const Event &event) {
        if (event.depth != 1 || event.type != Type::String) {
            return;
        }
        const auto &key = event.key.value();
        const auto &value = event.value.value();
        if (key == "gcode") {
            const size_t len = std::min(value.size(), sizeof(gcode) - 1);
            memcpy(gcode, value.data(), len);
            gcode[len] = '\0';
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

    if (gcode[0] == '\0') {
        return StatusPage(Status::BadRequest, close_handling, json_errors, nullopt, "Missing gcode command");
    }

    switch (inject(gcode)) {
    case InjectResult::Submitted:
        return StatusPage(Status::NoContent, close_handling, json_errors);
    case InjectResult::QueueFull:
        return StatusPage(Status::Conflict, close_handling, json_errors, nullopt, "G-code queue full");
    case InjectResult::TooLong:
        return StatusPage(Status::BadRequest, close_handling, json_errors, nullopt, "G-code too long");
    default:
        assert(0);
        return StatusPage(Status::InternalServerError, close_handling, json_errors, nullopt, "Invalid gcode result");
    }
}

} // namespace nhttp::printer
