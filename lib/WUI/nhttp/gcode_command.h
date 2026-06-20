#pragma once

#include "status_page.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace nhttp::printer {

/**
 * \brief The "implementation" of GcodeCommand/POST to /api/v1/gcode.
 *
 * Reads a JSON body of the form {"gcode": "<command>"} and injects the
 * command into the Marlin queue. Intended for debugging - sending arbitrary
 * G-code and, through it, setting target temperatures (M104/M140) etc.
 */
class GcodeCommand {
public:
    enum class InjectResult {
        Submitted,
        QueueFull,
        TooLong,
    };

private:
    /*
     * Our current json parser expects to get the whole input as one block of
     * memory. Until/unless we replace it with something streaming/incremental,
     * we just gather the data here. A single G-code line is short, so this is
     * plenty even with the JSON wrapping around it.
     */
    static const constexpr size_t BUFFER_LEN = 200;
    std::array<uint8_t, BUFFER_LEN> buffer;
    size_t buffer_used = 0;
    size_t content_length;
    bool can_keep_alive;
    bool json_errors;

    handler::StatusPage process();

    /*
     * Implementation note: due to the fact this talks to marlin, it lives in a
     * separate cpp file and is replaced in tests (same approach as JobCommand).
     */
    InjectResult inject(const char *gcode);

public:
    GcodeCommand(size_t content_length, bool can_keep_alive, bool json_errors);
    bool want_read() const { return true; }
    bool want_write() const { return false; }
    void step(std::string_view input, bool terminated_by_client, uint8_t *buffer, size_t buffer_size, handler::Step &out);
};

} // namespace nhttp::printer
