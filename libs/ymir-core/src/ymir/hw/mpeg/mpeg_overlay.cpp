#include <ymir/hw/mpeg/mpeg_overlay.hpp>

#include <algorithm>
#include <cassert>

namespace ymir::mpeg {

void MPEGVideoOverlay::BlitLatestFrame(const MPEGCard &card, std::span<uint32> framebuffer, uint32 fbWidth,
                                       uint32 fbHeight) {
    // Keep displaying the last decoded frame while the card is Playing or
    // has naturally Ended (user pressed Start, or stream reached EOF).
    // The frame persists as a backdrop during the hand-off until the game
    // explicitly stops the decoder (CmdMpegStopDecoder -> Reset -> Stopped),
    // at which point VDP2 takes over with the title screen.
    //
    // BUT: honor the $A0 MpegDisplay switch. When the game turns the MPEG
    // display off (CR2 high byte = 0), the overlay must stop blitting so
    // VDP2's own layers (title screen, menus, etc.) become visible. This
    // matches the real hardware's EXBG path: the external image is only
    // composited while dispOn is asserted.
    if (!card.IsDisplayEnabled()) {
        return;
    }
    if (card.GetStatus() != MPEGCardStatus::Playing &&
        card.GetStatus() != MPEGCardStatus::Ended) {
        return;
    }
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
