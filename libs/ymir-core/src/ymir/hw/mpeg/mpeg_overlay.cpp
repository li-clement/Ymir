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
                                        uint32 fbHeight, uint32 internalScale, const MPEGWindowState *window) {
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

    internalScale = std::max(internalScale, 1u);
    const uint32 fbW = fbWidth / internalScale;
    const uint32 fbH = fbHeight / internalScale;
    if (fbW == 0 || fbH == 0) {
        return;
    }

    if (window == nullptr || !window->IsConfigured()) {
        // No window configured (Lunar / Vatlva): 1:1 full-frame blit, clipped
        // to the VDP2 framebuffer. The Movie Card's frame buffer is treated
        // as a same-size match for the screen; we copy up to the smaller of
        // the source and destination extents.
        const uint32 copyW = std::min<uint32>(frame.width, fbW);
        const uint32 copyH = std::min<uint32>(frame.height, fbH);
        if (copyW == 0 || copyH == 0) {
            return;
        }

        for (uint32 y = 0; y < copyH; ++y) {
            const uint32 *srcRow = frame.pixelsXBGR8888.data() + static_cast<size_t>(y) * frame.width;
            for (uint32 scaleY = 0; scaleY < internalScale; ++scaleY) {
                uint32 *dstRow = framebuffer.data() + static_cast<size_t>(y * internalScale + scaleY) * fbWidth;
                for (uint32 x = 0; x < copyW; ++x) {
                    std::fill_n(dstRow + x * internalScale, internalScale, srcRow[x]);
                }
            }
        }
        return;
    }

    // Windowed path (Moon Cradle): composite a sub-rectangle of the Movie
    // Card frame buffer onto the VDP2 framebuffer at the configured display
    // position/size/ratio.
    //
    // Source step per display pixel in thousandths (1000 = 1:1).
    const uint32 srcStepX = DecodeRatio(window->fbRatioX);
    const uint32 srcStepY = DecodeRatio(window->fbRatioY);
    // Display position/size are in the Movie Card's output raster
    // coordinates. The raster is fixed (240 lines NTSC / 256 lines PAL) but
    // the VDP2 framebuffer height varies by screen mode (224 or 240 lines).
    // The Movie Card output raster sits centred on the full raster, so the
    // Y origin needs a screen-mode-dependent correction (erings):
    //   decoder_Y -= (raster - framebuffer_height) / 2
    // For Lunar (224-line screen, dispPosY=$08) this gives Y=0; for
    // Moon Cradle (240-line screen, dispPosY=$28) it gives Y=40. The X
    // origin sits one dot left of the frame edge (erings: traced on 320-wide
    // screens, a flush-left title programs X=-1, so add 1 to land on X=0).
    //
    // We pass fbH (the framebuffer height in screen pixels, before internal
    // resolution scaling) as the screen mode proxy: this matches the host's
    // VDP2 framebuffer layout that the renderer has already configured.
    const sint32 fbHeightPx = static_cast<sint32>(fbH);
    const sint32 rasterH = 240; // NTSC; PAL is untraced for our targets
    const sint32 yOrigin = (rasterH - fbHeightPx) / 2;
    const sint32 dispX = static_cast<sint32>(window->dispPosX) + 1;
    const sint32 dispY = static_cast<sint32>(window->dispPosY) - yOrigin;
    const uint32 dispW = window->dispSizeW;
    const uint32 dispH = window->dispSizeH;

    const sint32 fbPosX = static_cast<sint32>(window->fbPosX);
    const sint32 fbPosY = static_cast<sint32>(window->fbPosY);

    // Pre-clip the visible window against the VDP2 framebuffer. Pixels
    // outside the visible region stay transparent (left untouched by the
    // caller -- the VDP2 layer beneath will show through).
    const sint32 dstX0 = dispX;
    const sint32 dstY0 = dispY;
    const sint32 dstX1 = dispX + static_cast<sint32>(dispW);
    const sint32 dstY1 = dispY + static_cast<sint32>(dispH);
    const sint32 clipX0 = 0;
    const sint32 clipY0 = 0;
    const sint32 clipX1 = static_cast<sint32>(fbW);
    const sint32 clipY1 = static_cast<sint32>(fbH);

    const sint32 visX0 = std::max(dstX0, clipX0);
    const sint32 visY0 = std::max(dstY0, clipY0);
    const sint32 visX1 = std::min(dstX1, clipX1);
    const sint32 visY1 = std::min(dstY1, clipY1);
    if (visX1 <= visX0 || visY1 <= visY0) {
        return; // window fully outside the screen
    }

    for (sint32 y = visY0; y < visY1; ++y) {
        // Map display-Y to source-Y. The frame-buffer anchor is fbPosY; the
        // ratio-stepped offset is (y - dispY) * srcStepY / 1000.
        const sint32 srcY = fbPosY + ((y - dstY0) * static_cast<sint32>(srcStepY)) / 1000;
        if (srcY < 0 || srcY >= static_cast<sint32>(frame.height)) {
            continue;
        }
        const uint32 *srcRow = frame.pixelsXBGR8888.data() + static_cast<size_t>(srcY) * frame.width;
        for (uint32 scaleY = 0; scaleY < internalScale; ++scaleY) {
            const uint32 dstRowIdx = static_cast<uint32>(y) * internalScale + scaleY;
            uint32 *dstRow = framebuffer.data() + static_cast<size_t>(dstRowIdx) * fbWidth;
            for (sint32 x = visX0; x < visX1; ++x) {
                const sint32 srcX = fbPosX + ((x - dstX0) * static_cast<sint32>(srcStepX)) / 1000;
                if (srcX < 0 || srcX >= static_cast<sint32>(frame.width)) {
                    continue;
                }
                const uint32 pixel = srcRow[srcX];
                for (uint32 scaleX = 0; scaleX < internalScale; ++scaleX) {
                    dstRow[static_cast<uint32>(x) * internalScale + scaleX] = pixel;
                }
            }
        }
    }
    (void)fbHeight;
}

} // namespace ymir::mpeg