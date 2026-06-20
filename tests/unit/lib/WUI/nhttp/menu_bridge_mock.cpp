#include <menu_bridge.hpp>

// Stub of the menu bridge for the WUI unit tests. The real implementation lives
// on the GUI thread (src/gui/menu_bridge.cpp) and pulls in the whole GUI stack,
// which is not available here.
namespace menu_bridge {

void process() {}

const char *acquire_snapshot(size_t &length_out) {
    static const char snapshot[] = "{\"open\":false}";
    length_out = sizeof(snapshot) - 1;
    return snapshot;
}

void release_snapshot() {}

bool post_action(ActionOp, int, float, bool) {
    return true;
}

} // namespace menu_bridge
