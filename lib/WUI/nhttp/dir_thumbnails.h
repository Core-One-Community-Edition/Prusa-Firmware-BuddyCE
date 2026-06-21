#pragma once

#include <segmented_json.h>
#include <directory.hpp>
#include <gcode/gcode_reader_any.hpp>

// FILE_PATH_BUFFER_LEN lives in gui.
#include "../../src/gui/file_list_defs.h"

#include <cstdint>

namespace nhttp::printer {

/**
 * \brief State for [DirThumbnails].
 *
 * Holds the directory iterator and, per entry, the base64 of its small
 * thumbnail (read + encoded once when the entry is first rendered, kept here so
 * it survives a render resume). [gcode] is reused to open each file in turn.
 */
struct DirThumbnailsState {
    // Cap on a single entry's base64 so the "name":"<base64>" field always fits
    // one send buffer (TCP_MSS); larger thumbnails are skipped. 16x16 PNGs are
    // far smaller than this in practice.
    static constexpr size_t base64_cap = 896;

    char filepath[FILE_PATH_BUFFER_LEN] {};
    Directory dir;
    AnyGcodeFormatReader gcode;
    dirent *ent = nullptr;
    bool first = true;
    char base64[base64_cap + 1] {};

    DirThumbnailsState() = default;
    explicit DirThumbnailsState(const char *path);
    DirThumbnailsState(DirThumbnailsState &&) = default;
    DirThumbnailsState &operator=(DirThumbnailsState &&) = default;
};

/**
 * \brief Renders every printable file's small (16x16) thumbnail in a directory
 * as a single JSON object: `{ "<short name>": "<base64 png>", ... }`.
 *
 * Lets the client fetch a whole directory's thumbnails in one request instead
 * of one per file. Files without a 16x16 thumbnail (or whose base64 exceeds
 * [DirThumbnailsState::base64_cap]) are omitted.
 */
class DirThumbnails final : public json::JsonRenderer<DirThumbnailsState> {
protected:
    json::JsonResult renderState(size_t resume_point, json::JsonOutput &output, DirThumbnailsState &state) const override;

public:
    explicit DirThumbnails(const char *path)
        : JsonRenderer(DirThumbnailsState(path)) {}
};

} // namespace nhttp::printer
