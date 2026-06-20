#include "gcode_command.h"

#include <marlin_client.hpp>

namespace nhttp::printer {

GcodeCommand::InjectResult GcodeCommand::inject(const char *gcode) {
    switch (marlin_client::gcode_try(gcode)) {
    case marlin_client::GcodeTryResult::Submitted:
        return InjectResult::Submitted;
    case marlin_client::GcodeTryResult::QueueFull:
        return InjectResult::QueueFull;
    case marlin_client::GcodeTryResult::GcodeTooLong:
        return InjectResult::TooLong;
    }
    return InjectResult::QueueFull;
}

} // namespace nhttp::printer
