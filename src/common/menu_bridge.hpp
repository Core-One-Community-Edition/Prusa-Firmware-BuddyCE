#pragma once

#include <cstddef>
#include <cstdint>

/// Bridge that exposes the live GUI menu over the PrusaLink HTTP API.
///
/// The GUI thread publishes a JSON snapshot of the currently displayed menu and
/// applies queued actions; the networking thread only reads the snapshot and
/// queues actions, so the (event-driven, non-blocking) HTTP server never blocks
/// on the GUI thread.
namespace menu_bridge {

enum class ActionOp : uint8_t {
    click,
    set_number,
    set_toggle,
    back,
};

/// GUI thread: apply any pending action, then refresh the serialized snapshot
/// (throttled, and skipped while a reader holds the snapshot).
void process();

/// Net thread: borrow the current menu JSON snapshot. The returned pointer
/// stays valid and is not overwritten until release_snapshot() is called.
/// Always pair with release_snapshot().
const char *acquire_snapshot(size_t &length_out);

/// Net thread: release a snapshot previously taken with acquire_snapshot().
void release_snapshot();

/// Net thread: queue an action for the GUI thread to apply. Returns false if an
/// action is already pending (caller should report "busy").
bool post_action(ActionOp op, int index, float number, bool flag);

} // namespace menu_bridge
