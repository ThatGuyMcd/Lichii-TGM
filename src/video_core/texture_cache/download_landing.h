// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "video_core/rasterizer_download_area.h"

namespace VideoCommon {

// A CPU write collected before the landing keeps its image cached. A later write waits for the
// landing before storing, so downloaded GPU bytes cannot overwrite the guest's newer bytes.
template <typename VisitWrites, typename Store>
bool LandTextureDownload(DAddr begin, DAddr end, VisitWrites&& visit_writes, Store&& store) {
    const VideoCore::DownloadLanding landing{
        begin, end, VideoCore::DownloadLanding::Source::TextureCache};
    bool cpu_written = false;
    visit_writes([&](DAddr address, size_t size) {
        if (size != 0 && address < end && (address >= begin || size > begin - address)) {
            cpu_written = true;
        }
    });
    if (cpu_written) {
        return false;
    }
    store();
    return true;
}

} // namespace VideoCommon
