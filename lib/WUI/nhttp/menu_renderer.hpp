#pragma once

#include <segmented_json.h>
#include <menu_bridge.hpp>

#include <cstring>
#include <tuple>
#include <utility>

namespace nhttp {

/// Streams the menu_bridge JSON snapshot as an HTTP response body. It borrows
/// the snapshot for the whole (possibly chunked) send via acquire/release, so
/// the GUI thread won't overwrite it mid-stream.
class MenuJsonRenderer final : public json::ChunkRenderer {
public:
    MenuJsonRenderer() = default;
    MenuJsonRenderer(const MenuJsonRenderer &) = delete;
    MenuJsonRenderer &operator=(const MenuJsonRenderer &) = delete;

    MenuJsonRenderer(MenuJsonRenderer &&other) {
        *this = std::move(other);
    }
    MenuJsonRenderer &operator=(MenuJsonRenderer &&other) {
        if (this != &other) {
            release();
            offset = other.offset;
            length = other.length;
            data = other.data;
            started = other.started;
            acquired = other.acquired;
            other.acquired = false;
            other.data = nullptr;
        }
        return *this;
    }

    ~MenuJsonRenderer() {
        release();
    }

    std::tuple<json::JsonResult, size_t> render(uint8_t *buffer, size_t buffer_size) override {
        if (!started) {
            data = menu_bridge::acquire_snapshot(length);
            acquired = true;
            started = true;
        }
        if (buffer_size == 0) {
            return std::make_tuple(json::JsonResult::BufferTooSmall, size_t { 0 });
        }

        const size_t remaining = length - offset;
        const size_t to_copy = (remaining < buffer_size) ? remaining : buffer_size;
        memcpy(buffer, data + offset, to_copy);
        offset += to_copy;

        if (offset >= length) {
            release();
            return std::make_tuple(json::JsonResult::Complete, to_copy);
        }
        return std::make_tuple(json::JsonResult::Incomplete, to_copy);
    }

private:
    void release() {
        if (acquired) {
            menu_bridge::release_snapshot();
            acquired = false;
        }
    }

    size_t offset = 0;
    size_t length = 0;
    const char *data = nullptr;
    bool started = false;
    bool acquired = false;
};

} // namespace nhttp
