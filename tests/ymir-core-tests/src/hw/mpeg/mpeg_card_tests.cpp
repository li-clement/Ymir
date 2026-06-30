#include <ymir/hw/mpeg/mpeg_card.hpp>

#include <catch2/catch_test_macros.hpp>

#include "mpeg_test_fixtures.hpp"

using namespace ymir;
using namespace ymir::test::mpeg_fixtures;

TEST_CASE("MPEG movie card accepts streamed sector payloads and exposes decoded frames", "[mpeg][movie-card]") {
    mpeg::MPEGCard card;

    card.Initialize();
    card.StartPlayback();

    constexpr size_t kSplit = 1024;
    card.AppendStreamData(std::span<const uint8>{kTinyMpegProgramStream}.first(kSplit));
    card.AppendStreamData(std::span<const uint8>{kTinyMpegProgramStream}.subspan(kSplit));
    card.SignalEndOfStream();

    REQUIRE(card.HasHeaders());
    CHECK(card.GetWidth() == 16);
    CHECK(card.GetHeight() == 16);

    REQUIRE(card.DecodeNextFrame());
    REQUIRE(card.HasCurrentFrame());

    const auto &frame = card.GetCurrentFrame();
    CHECK(frame.width == 16);
    CHECK(frame.height == 16);
    CHECK(frame.pixelsXBGR8888.size() == 16 * 16);

    CHECK((card.PeekInterruptFlags() & mpeg::kMPEGCardInterruptFrameDecoded) != 0);
    CHECK((card.TakeInterruptFlags() & mpeg::kMPEGCardInterruptFrameDecoded) != 0);
    CHECK(card.PeekInterruptFlags() == 0);
}

TEST_CASE("MPEG movie card does not decode while stopped and reset clears state", "[mpeg][movie-card]") {
    mpeg::MPEGCard card;

    card.Initialize();
    card.AppendStreamData(kTinyMpegProgramStream);
    card.SignalEndOfStream();

    CHECK_FALSE(card.DecodeNextFrame());
    CHECK_FALSE(card.HasCurrentFrame());
    CHECK(card.GetStatus() == mpeg::MPEGCardStatus::Stopped);

    card.StartPlayback();
    REQUIRE(card.DecodeNextFrame());
    CHECK(card.HasCurrentFrame());
    CHECK(card.PeekInterruptFlags() != 0);

    card.Reset();
    CHECK(card.GetStatus() == mpeg::MPEGCardStatus::Stopped);
    CHECK_FALSE(card.HasHeaders());
    CHECK_FALSE(card.HasCurrentFrame());
    CHECK(card.PeekInterruptFlags() == 0);
}
