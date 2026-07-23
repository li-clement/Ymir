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
