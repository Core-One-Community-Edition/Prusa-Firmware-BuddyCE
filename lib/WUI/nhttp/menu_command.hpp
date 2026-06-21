#pragma once

#include "status_page.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace nhttp::printer {

/**
 * \brief Handler for POST /api/v1/menu.
 *
 * Reads a JSON body describing a menu action and queues it on the GUI thread
 * via menu_bridge. Body shapes:
 *   {"op":"click","index":N}
 *   {"op":"set","index":N,"number":215}
 *   {"op":"set","index":N,"bool":true}
 *   {"op":"back"}
 */
class MenuCommand {
private:
    static const constexpr size_t BUFFER_LEN = 200;
    std::array<uint8_t, BUFFER_LEN> buffer;
    size_t buffer_used = 0;
    size_t content_length;
    bool can_keep_alive;
    bool json_errors;

    handler::StatusPage process();

public:
    MenuCommand(size_t content_length, bool can_keep_alive, bool json_errors);
    bool want_read() const { return true; }
    bool want_write() const { return false; }
    void step(std::string_view input, bool terminated_by_client, uint8_t *buffer, size_t buffer_size, handler::Step &out);
};

} // namespace nhttp::printer
