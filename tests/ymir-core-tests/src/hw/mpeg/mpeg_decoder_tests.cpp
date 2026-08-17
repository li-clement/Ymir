#include <ymir/hw/mpeg/mpeg_decoder.hpp>

#include <catch2/catch_test_macros.hpp>

#include "mpeg_test_fixtures.hpp"

#include <span>

using namespace ymir;

using namespace ymir::test::mpeg_fixtures;

TEST_CASE("MPEG movie card decoder accepts a program stream and decodes video frames", "[mpeg][movie-card]") {
    mpeg::MPEGVideoDecoder decoder;

    decoder.Append(kTinyMpegProgramStream);
    decoder.SignalEndOfStream();

    REQUIRE(decoder.HasHeaders());
    CHECK(decoder.GetWidth() == 16);
    CHECK(decoder.GetHeight() == 16);
    CHECK(decoder.GetFrameRate() > 0.0);

    auto frame = decoder.DecodeFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->width == 16);
    CHECK(frame->height == 16);
    REQUIRE(frame->pixelsXBGR8888.size() == 16 * 16);

    bool sawNonBlackPixel = false;
    for (uint32 pixel : frame->pixelsXBGR8888) {
        CHECK((pixel & 0xFF000000u) == 0xFF000000u);
        sawNonBlackPixel = sawNonBlackPixel || ((pixel & 0x00FFFFFFu) != 0);
    }
    CHECK(sawNonBlackPixel);
}

TEST_CASE("MPEG movie card decoder can be reset and reused", "[mpeg][movie-card]") {
    mpeg::MPEGVideoDecoder decoder;
    decoder.Append(kTinyMpegProgramStream);
    decoder.SignalEndOfStream();
    REQUIRE(decoder.DecodeFrame().has_value());

    decoder.Reset();
    CHECK_FALSE(decoder.HasHeaders());

    decoder.Append(kTinyMpegProgramStream);
    decoder.SignalEndOfStream();
    REQUIRE(decoder.HasHeaders());
    REQUIRE(decoder.DecodeFrame().has_value());
}
