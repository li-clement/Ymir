#include <ymir/hw/mpeg/mpeg_overlay.hpp>

#include <algorithm>
#include <cassert>

namespace ymir::mpeg {

void MPEGVideoOverlay::BlitLatestFrame(std::span<uint32> framebuffer, uint32 fbWidth, uint32 fbHeight) {
    auto &card = MPEGCard::GetGlobal();
    if (!card.HasCurrentFrame()) {
        return;
    }
    const auto &frame = card.GetCurrentFrame();
    if (frame.width == 0 || frame.height == 0) {
        return;
    }
    if (frame.pixelsXBGR8888.size() != static_cast<size_t>(frame.width) * frame.height) {
        return;
    }

    const uint32 copyW = std::min<uint32>(frame.width, fbWidth);
    const uint32 copyH = std::min<uint32>(frame.height, fbHeight);
    if (copyW == 0 || copyH == 0) {
        return;
    }

    for (uint32 y = 0; y < copyH; ++y) {
        const uint32 *srcRow = frame.pixelsXBGR8888.data() + static_cast<size_t>(y) * frame.width;
        uint32 *dstRow = framebuffer.data() + static_cast<size_t>(y) * fbWidth;
        std::copy_n(srcRow, copyW, dstRow);
    }
    (void)fbHeight;
}

} // namespace ymir::mpeg
