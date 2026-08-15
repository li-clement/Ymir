#include <ymir/hw/mpeg/mpeg_overlay.hpp>
#include <ymir/hw/mpeg/mpeg_card.hpp>

#include <catch2/catch_test_macros.hpp>

#include "mpeg_test_fixtures.hpp"

#include <array>
#include <span>

using namespace ymir;
using namespace ymir::test::mpeg_fixtures;

TEST_CASE("MPEG video overlay blits latest decoded frame into a software framebuffer", "[mpeg][movie-card][overlay]") {
    mpeg::MPEGCard card;
    card.Initialize();
    card.AppendStreamData(kTinyMpegProgramStream);
    card.SignalEndOfStream();
    card.StartPlayback();
    card.SetDisplayEnabled(true);
    REQUIRE(card.DecodeNextFrame());
    REQUIRE(card.HasCurrentFrame());
    REQUIRE(card.GetCurrentFrame().width == 16);
    REQUIRE(card.GetCurrentFrame().height == 16);

    std::array<uint32, 32 * 32> fb{};
    fb.fill(0xFF112233u);

    mpeg::MPEGVideoOverlay overlay;
    overlay.BlitLatestFrame(card, std::span<uint32>{fb.data(), fb.size()}, 32, 32);

    for (uint32 y = 0; y < 16; ++y) {
        for (uint32 x = 0; x < 16; ++x) {
            const uint32 pixel = fb[y * 32 + x];
            CHECK((pixel & 0xFF000000u) == 0xFF000000u);
        }
    }
    for (uint32 y = 0; y < 32; ++y) {
        for (uint32 x = 16; x < 32; ++x) {
            CHECK(fb[y * 32 + x] == 0xFF112233u);
        }
    }
}

TEST_CASE("MPEG video overlay does nothing when no frame is decoded", "[mpeg][movie-card][overlay]") {
    mpeg::MPEGCard card;
    card.Reset();

    std::array<uint32, 16 * 16> fb{};
    fb.fill(0xFF112233u);

    mpeg::MPEGVideoOverlay overlay;
    overlay.BlitLatestFrame(card, std::span<uint32>{fb.data(), fb.size()}, 16, 16);
    CHECK(fb[0] == 0xFF112233u);
}

TEST_CASE("MPEG video overlay scales frames to the internal framebuffer resolution", "[mpeg][movie-card][overlay]") {
    mpeg::MPEGCard card;
    card.Initialize();
    card.AppendStreamData(kTinyMpegProgramStream);
    card.SignalEndOfStream();
    card.StartPlayback();
    card.SetDisplayEnabled(true);
    REQUIRE(card.DecodeNextFrame());

    constexpr uint32 kScale = 2;
    constexpr uint32 kFrameSize = 16;
    std::array<uint32, kFrameSize * kScale * kFrameSize * kScale> fb{};
    fb.fill(0xFF112233u);

    mpeg::MPEGVideoOverlay overlay;
    overlay.BlitLatestFrame(card, std::span<uint32>{fb.data(), fb.size()}, kFrameSize * kScale, kFrameSize * kScale,
                            kScale);

    const auto &frame = card.GetCurrentFrame();
    for (uint32 y = 0; y < kFrameSize; ++y) {
        for (uint32 x = 0; x < kFrameSize; ++x) {
            const uint32 pixel = frame.pixelsXBGR8888[y * kFrameSize + x];
            CHECK(fb[(y * kScale) * kFrameSize * kScale + x * kScale] == pixel);
            CHECK(fb[(y * kScale) * kFrameSize * kScale + x * kScale + 1] == pixel);
            CHECK(fb[(y * kScale + 1) * kFrameSize * kScale + x * kScale] == pixel);
            CHECK(fb[(y * kScale + 1) * kFrameSize * kScale + x * kScale + 1] == pixel);
        }
    }
}

TEST_CASE("MPEG video overlay does not blit when display is disabled",
          "[mpeg][movie-card][overlay]") {
    mpeg::MPEGCard card;
    card.Initialize();
    card.AppendStreamData(kTinyMpegProgramStream);
    card.SignalEndOfStream();
    card.StartPlayback();
    card.SetDisplayEnabled(true);
    REQUIRE(card.DecodeNextFrame());

    // Game issues $A0 MpegDisplay off -> overlay must stop blitting.
    card.SetDisplayEnabled(false);

    std::array<uint32, 32 * 32> fb{};
    fb.fill(0xFF112233u);

    mpeg::MPEGVideoOverlay overlay;
    overlay.BlitLatestFrame(card, std::span<uint32>{fb.data(), fb.size()}, 32, 32);

    // Framebuffer untouched: the last FMV frame must not cover the title screen.
    for (uint32 i = 0; i < fb.size(); ++i) {
        CHECK(fb[i] == 0xFF112233u);
    }
}

TEST_CASE("MPEG video overlay honours the $A1 EXBG display window for Moon Cradle",
          "[mpeg][movie-card][overlay][window]") {
    // Build a synthetic frame buffer large enough to cover the window.
    // The Movie Card's frame buffer is wider than the visible window
    // (erings: it stores full pictures and the window picks a sub-rectangle
    // to composite). For this test we use a 512x256 source so the
    // window's frame-buffer anchor at (32, 40) and 288x160 visible extent
    // stay fully inside the source rectangle.
    constexpr uint32 kSrcW = 512;
    constexpr uint32 kSrcH = 256;
    mpeg::DecodedVideoFrame frame{};
    frame.width = kSrcW;
    frame.height = kSrcH;
    frame.pixelsXBGR8888.resize(kSrcW * kSrcH);
    for (uint32 y = 0; y < kSrcH; ++y) {
        for (uint32 x = 0; x < kSrcW; ++x) {
            // Distinct pixel per (x,y) so we can verify the window mapping.
            frame.pixelsXBGR8888[y * kSrcW + x] = 0xFF000000u | (y << 16) | (x << 8) | 0x80u;
        }
    }

    mpeg::MPEGCard card;
    card.Initialize();
    card.StartPlayback();
    card.SetCurrentFrameForTest(frame);
    card.SetDisplayEnabled(true);

    // Moon Cradle opening FMV: 320x240 screen, picture at (15, 40) sized
    // 288x160 (frame-buffer pos $20/$28, 1:1 ratio, display pos $0F/$28,
    // display size 288x160). The screen size of the test VDP2 surface is
    // arbitrary -- we just verify the visible rectangle gets the right
    // pixels and the surrounding area stays untouched.
    mpeg::MPEGWindowState window{};
    window.fbPosX = 0x20;
    window.fbPosY = 0x28;
    window.fbRatioX = 0x8011; // 1:1
    window.fbRatioY = 0x8011;
    window.dispPosX = 0x0F;
    window.dispPosY = 0x28;
    window.dispSizeW = 0x0120; // 288
    window.dispSizeH = 0x00A0; // 160

    constexpr uint32 kFbW = 320;
    constexpr uint32 kFbH = 240;
    std::array<uint32, kFbW * kFbH> fb{};
    fb.fill(0xFF112233u);

    mpeg::MPEGVideoOverlay overlay;
    overlay.BlitLatestFrame(card, std::span<uint32>{fb.data(), fb.size()}, kFbW, kFbH, 1, &window);

    // The overlay applies a one-dot X origin correction (erings: decoder
    // X sits one dot left of the frame edge) and a screen-mode-dependent
    // Y origin correction (decoder Y sits centred on the 240-line raster,
    // so a 240-line screen has yOrigin=0 and a 224-line screen has
    // yOrigin=8). Account for both when verifying the visible window.
    const sint32 exbgDX = window.dispPosX + 1;
    const sint32 exbgDY = window.dispPosY - (240 - static_cast<sint32>(kFbH)) / 2;
    const sint32 visX0 = std::max<sint32>(exbgDX, 0);
    const sint32 visY0 = std::max<sint32>(exbgDY, 0);
    const sint32 visX1 = std::min<sint32>(exbgDX + static_cast<sint32>(window.dispSizeW),
                                          static_cast<sint32>(kFbW));
    const sint32 visY1 = std::min<sint32>(exbgDY + static_cast<sint32>(window.dispSizeH),
                                          static_cast<sint32>(kFbH));

    // Outside the window: untouched (still the 0xFF112233 sentinel).
    for (uint32 y = 0; y < kFbH; ++y) {
        for (uint32 x = 0; x < kFbW; ++x) {
            const bool inside = (static_cast<sint32>(x) >= visX0 && static_cast<sint32>(x) < visX1 &&
                                 static_cast<sint32>(y) >= visY0 && static_cast<sint32>(y) < visY1);
            if (inside) {
                continue;
            }
            CHECK(fb[y * kFbW + x] == 0xFF112233u);
        }
    }

    // Inside the window: pixel at (x,y) should equal source at
    // (fbPosX + (x - exbgDX), fbPosY + (y - exbgDY)).
    for (sint32 y = visY0; y < visY1; ++y) {
        for (sint32 x = visX0; x < visX1; ++x) {
            const uint32 srcX = window.fbPosX + (x - exbgDX);
            const uint32 srcY = window.fbPosY + (y - exbgDY);
            const uint32 expected = frame.pixelsXBGR8888[srcY * kSrcW + srcX];
            CHECK(fb[static_cast<size_t>(y) * kFbW + static_cast<size_t>(x)] == expected);
        }
    }
}

TEST_CASE("MPEG video overlay with $A1 window clips to the screen and leaves the rest transparent",
          "[mpeg][movie-card][overlay][window]") {
    constexpr uint32 kSrcW = 64;
    constexpr uint32 kSrcH = 64;
    mpeg::DecodedVideoFrame frame{};
    frame.width = kSrcW;
    frame.height = kSrcH;
    frame.pixelsXBGR8888.resize(kSrcW * kSrcH, 0xFFAABBCCu);

    mpeg::MPEGCard card;
    card.Initialize();
    card.StartPlayback();
    card.SetCurrentFrameForTest(frame);
    card.SetDisplayEnabled(true);

    // Window that hangs off the right edge of the screen.
    mpeg::MPEGWindowState window{};
    window.fbPosX = 0;
    window.fbPosY = 0;
    window.fbRatioX = 0x8011;
    window.fbRatioY = 0x8011;
    window.dispPosX = 280;
    window.dispPosY = 200;
    window.dispSizeW = 80;
    window.dispSizeH = 80;

    constexpr uint32 kFbW = 320;
    constexpr uint32 kFbH = 240;
    std::array<uint32, kFbW * kFbH> fb{};
    fb.fill(0xFF112233u);

    mpeg::MPEGVideoOverlay overlay;
    overlay.BlitLatestFrame(card, std::span<uint32>{fb.data(), fb.size()}, kFbW, kFbH, 1, &window);

    // After the one-dot X origin correction the visible rectangle starts
    // at X=281 (and runs to the right edge at 320).
    // Pixels outside the visible rectangle stay untouched.
    for (uint32 y = 0; y < kFbH; ++y) {
        for (uint32 x = 0; x < kFbW; ++x) {
            const bool inside = (x >= 281 && x < 320 && y >= 200 && y < 240);
            if (!inside) {
                CHECK(fb[y * kFbW + x] == 0xFF112233u);
            }
        }
    }
    // Inside the visible rectangle (40x39 corner): pixels are written.
    CHECK(fb[200 * kFbW + 281] == 0xFFAABBCCu);
    CHECK(fb[239 * kFbW + 319] == 0xFFAABBCCu);
}
