#include "dir_thumbnails.h"

#include "../../src/common/filename_type.hpp"
#include <common/filepath_operation.h>
#include <segmented_json_macros.h>

#include <mbedtls/base64.h>

#include <cstdio>

using namespace json;

namespace nhttp::printer {

DirThumbnailsState::DirThumbnailsState(const char *path)
    : dir(path) {
    snprintf(filepath, sizeof(filepath), "%s", path);
}

JsonResult DirThumbnails::renderState(size_t resume_point, JsonOutput &output, DirThumbnailsState &state) const {
    // Keep the indentation of the JSON in here!
    // clang-format off
    JSON_START;
    JSON_OBJ_START;
        while (state.dir && (state.ent = state.dir.read())) {
            if (const char *lfn = dirent_lfn(state.ent); lfn && lfn[0] == '.') {
                continue;
            }
            // Only printable gcode/bgcode files carry thumbnails.
            if (state.ent->d_type == DT_DIR || !filename_is_printable(state.ent->d_name)) {
                continue;
            }

            // Read + base64-encode the small thumbnail once, into state (so it
            // survives a render resume). Locals are scoped so they don't cross
            // the JSON case labels below.
            {
                bool has_thumb = false;
                char path[FILE_PATH_BUFFER_LEN + FILE_NAME_BUFFER_LEN];
                snprintf(path, sizeof(path), "%s/%s", state.filepath, state.ent->d_name);

                if (state.gcode.open(path)) {
                    AbstractByteReader *reader = state.gcode->stream_thumbnail_start(16, 16, IGcodeReader::ImgType::PNG, false);
                    if (reader != nullptr) {
                        size_t b64len = 0;
                        bool overflow = false;
                        uint8_t raw[3];
                        size_t raw_n = 0;
                        while (true) {
                            const size_t got = reader->read({ raw + raw_n, 3 - raw_n }).size();
                            raw_n += got;
                            if (raw_n == 3) {
                                if (b64len + 5 > sizeof(state.base64)) {
                                    overflow = true;
                                    break;
                                }
                                size_t enc = 0;
                                mbedtls_base64_encode(reinterpret_cast<uint8_t *>(state.base64) + b64len, 5, &enc, raw, 3);
                                b64len += enc;
                                raw_n = 0;
                            } else if (got == 0) {
                                // EOF: encode the trailing 1-2 bytes as the final group.
                                if (raw_n > 0 && b64len + 5 <= sizeof(state.base64)) {
                                    size_t enc = 0;
                                    mbedtls_base64_encode(reinterpret_cast<uint8_t *>(state.base64) + b64len, 5, &enc, raw, raw_n);
                                    b64len += enc;
                                } else if (raw_n > 0) {
                                    overflow = true;
                                }
                                break;
                            }
                        }
                        if (!overflow && b64len > 0) {
                            state.base64[b64len] = '\0';
                            has_thumb = true;
                        }
                    }
                }
                state.gcode = AnyGcodeFormatReader {};

                if (!has_thumb) {
                    continue;
                }
            }

            if (!state.first) {
                JSON_COMMA;
            } else {
                state.first = false;
            }
            JSON_FIELD_STR(state.ent->d_name, state.base64);
        }
    JSON_OBJ_END;
    JSON_END;
    // clang-format on
}

} // namespace nhttp::printer
