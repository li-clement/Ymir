#pragma once

#include "mpeg_card.hpp"

#include <ymir/core/types.hpp>

#include <span>

namespace ymir::mpeg {

// $A1 MpegSetWindow latched values, consumed by the overlay when the host
// SCSP/CDBlock has forwarded them. Positions are signed (decoder raster
// coordinates); sizes are unsigned extents. dispSizeW==0 && dispSizeH==0
// means the window was never configured: the overlay falls back to a 1:1
// full-frame blit (Lunar / Vatlva path).
struct MPEGWindowState {
    sint16 fbPosX = 0;
    sint16 fbPosY = 0;
    uint16 fbRatioX = 0;
    uint16 fbRatioY = 0;
    sint16 dispPosX = 0;
    sint16 dispPosY = 0;
    uint16 dispSizeW = 0;
    uint16 dispSizeH = 0;

    [[nodiscard]] bool IsConfigured() const { return dispSizeW != 0 && dispSizeH != 0; }
};

// Overlays the latest decoded MPEG frame onto a software-renderer-style XBGR8888 framebuffer.
//
// This is a temporary MVP path: real EXBG / native EXBG integration with VDP2 will replace it.
class MPEGVideoOverlay {
public:
    // Blits the latest frame from the given MPEGCard instance, optionally
    // honouring the $A1 MpegSetWindow placement.
    // The caller must ensure the card lifetime exceeds the call.
    void BlitLatestFrame(const MPEGCard &card, std::span<uint32> framebuffer, uint32 fbWidth, uint32 fbHeight,
                         uint32 internalScale = 1, const MPEGWindowState *window = nullptr);
};

} // namespace ymir::mpeg
