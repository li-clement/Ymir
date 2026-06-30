#pragma once

#include "mpeg_card.hpp"

#include <ymir/core/types.hpp>

#include <span>

namespace ymir::mpeg {

// Overlays the latest decoded MPEG frame onto a software-renderer-style XBGR8888 framebuffer.
//
// This is a temporary MVP path: real EXBG / native EXBG integration with VDP2 will replace it.
class MPEGVideoOverlay {
public:
    void BlitLatestFrame(std::span<uint32> framebuffer, uint32 fbWidth, uint32 fbHeight);
};

} // namespace ymir::mpeg
