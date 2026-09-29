#include <ymir/hw/mpeg/mpeg_overlay.hpp>

#include <algorithm>
#include <cassert>

namespace ymir::mpeg {

namespace {

// Fraction table of the $A1 frame-buffer ratio encoding, indexed by the
// wire value's low nibble (erings mpegRatioFrac). The library's 1:1
// default encodes as $8011 (integer part 1 + fraction 1/1000 -> 1.0).
// Unusable entries (0, 8) decode to 1000 (1:1).
constexpr uint32 kRatioFrac[16] = {
    1000, 0, 500, 666, 750, 800, 833, 857,
    1000, 1000, 500, 333, 250, 200, 166, 142,
};

// Decode one $A1 ratio wire value to source step per display pixel in
// thousandths (1000 = 1:1). Format (per erings mpegDecodeRatio):
//   bits 4..13  integer part
//   bits 0..3   fraction index into kRatioFrac
//   bit 15      set = direct form (n/1000), clear = reciprocal form
//                (1 000 000 / (n + 1000))
// Unusable wire values decode to 1000 (1:1).
uint32 DecodeRatio(uint16 wire) {
    const uint32 frac = kRatioFrac[wire & 0xF];
    const uint32 integerPart = (wire >> 4) & 0x3FF;
    uint32 v = integerPart * 1000 + frac;
    if ((wire & 0x8000) == 0) {
        // Reciprocal form: 1 / (1 + v / 1000) = 1 000 000 / (v + 1000).
        if (v == 0) {
            return 1000;
        }
        v = 1000000u / (v + 1000u);
    }
    if (v == 0) {
        return 1000;
    }
    return v;
}

} // namespace

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
    // Vatlva leaves the MPEG display latch enabled after natural end and
    // immediately returns to VDP2 title rendering. Do not retain its last ES
    // frame over the title area. Lunar's PS path keeps the Ended hand-off
    // frame because its driver tears the card down explicitly.
    if (card.IsVideoES() && card.GetStatus() == MPEGCardStatus::Ended) {
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