#include <ymir/hw/cdblock/cdblock.hpp>
#include <ymir/hw/cdblock/cdblock_defs.hpp>
#include <ymir/media/disc.hpp>
#include <ymir/media/filesystem.hpp>
#include <ymir/media/binary_reader/binary_reader_mem.hpp>
#include <ymir/core/configuration.hpp>
#include <ymir/core/scheduler.hpp>
#include <ymir/sys/bus.hpp>

#include <catch2/catch_test_macros.hpp>

#include "mpeg_test_fixtures.hpp"

using namespace ymir;
using namespace ymir::test::mpeg_fixtures;

namespace {

struct CDBlockHarness {
    core::Scheduler scheduler;
    media::Disc disc;
    media::fs::Filesystem fs;
    core::Configuration::CDBlock config;
    cdblock::CDBlock cdb{scheduler, disc, fs, config};
    sys::SH2Bus bus;
    uint32 extInterruptCount = 0;

    CDBlockHarness() {
        cdb.MapCallbacks(
            {this, [](void *ctx) { static_cast<CDBlockHarness *>(ctx)->extInterruptCount++; }},
            {this, [](std::span<uint8, 2352>, void *) -> uint32 { return 0; }});
        cdb.MapMemory(bus);
    }

    void RunCommand(uint16 cr1, uint16 cr2 = 0, uint16 cr3 = 0, uint16 cr4 = 0) {
        bus.Write<uint16>(0x5890018, cr1);
        bus.Write<uint16>(0x589001C, cr2);
        bus.Write<uint16>(0x5890020, cr3);
        bus.Write<uint16>(0x5890024, cr4);
        scheduler.Advance(50);
    }

    uint16 RR(uint32 index) const {
        return bus.Peek<uint16>(0x5890028 + index * sizeof(uint32));
    }

    uint16 HIRQ() const {
        return bus.Peek<uint16>(0x5890008);
    }
};


std::vector<uint8> BuildMode1DiscImage(std::span<const uint8> payload) {
    constexpr uint32 kSectorSize = 2048;
    const uint32 sectorCount = (payload.size() + kSectorSize - 1) / kSectorSize;
    std::vector<uint8> image(static_cast<size_t>(sectorCount) * kSectorSize, 0);
    std::copy(payload.begin(), payload.end(), image.begin());
    return image;
}

void ConfigureSingleDataTrack(media::Disc &disc, std::vector<uint8> image, uint32 sectorCount) {
    media::Session session{};
    session.numTracks = 1;
    session.firstTrackIndex = 0;
    session.lastTrackIndex = 0;
    session.startFrameAddress = 150;
    session.endFrameAddress = 150 + sectorCount - 1;

    auto &track = session.tracks[0];
    track.binaryReader = std::make_unique<media::MemoryBinaryReader>(std::move(image));
    track.SetSectorSize(2048);
    track.controlADR = 0x41;
    track.startFrameAddress = session.startFrameAddress;
    track.endFrameAddress = session.endFrameAddress;
    track.index01FrameAddress = session.startFrameAddress;
    track.indices = {{.startFrameAddress = session.startFrameAddress, .endFrameAddress = session.endFrameAddress}};

    session.BuildTOC();
    disc.sessions.clear();
    disc.sessions.push_back(std::move(session));
}

} // namespace

TEST_CASE("CD Block MPEG commands expose a minimal authenticated Movie Card", "[mpeg][movie-card][cdblock]") {
    CDBlockHarness h;

    h.RunCommand(0xE000, 0x0001); // Authenticate MPEG device
    CHECK((h.HIRQ() & cdblock::kHIRQ_CMOK) != 0);

    h.RunCommand(0xE100, 0x0001); // Is MPEG device authenticated?
    CHECK(h.RR(1) == 0x0002);

    h.RunCommand(0x0100); // Get hardware info exposes MPEG version after auth
    CHECK((h.RR(2) & 0x00FF) != 0x0000);

    h.RunCommand(0x9300); // MPEG init
    CHECK(h.RR(0) != 0xFF00);
    CHECK((h.HIRQ() & cdblock::kHIRQ_MPED) != 0);

    h.RunCommand(0x9000); // MPEG get status
    CHECK(h.RR(0) != 0xFF00);
}

TEST_CASE("CD Block MPEG stream commands feed Movie Card decoder", "[mpeg][movie-card][cdblock]") {
    CDBlockHarness h;

    h.RunCommand(0xE000, 0x0001);
    h.RunCommand(0x9300);
    h.RunCommand(0x9500); // MPEG play

    h.cdb.GetMPEGCard().AppendStreamData(kTinyMpegProgramStream);
    h.cdb.GetMPEGCard().SignalEndOfStream();
    REQUIRE(h.cdb.GetMPEGCard().DecodeNextFrame());
    REQUIRE(h.cdb.GetMPEGCard().HasCurrentFrame());
    CHECK(h.cdb.GetMPEGCard().GetCurrentFrame().width == 16);
    CHECK(h.cdb.GetMPEGCard().GetCurrentFrame().height == 16);

    h.RunCommand(0x9100); // MPEG get interrupt
    CHECK((h.RR(1) & 0x0001) != 0);

    h.RunCommand(0x9200, 0x0001); // clear frame decoded interrupt through mask command MVP
    h.RunCommand(0x9100);
    CHECK((h.RR(1) & 0x0001) == 0);
}


TEST_CASE("CD Block playback streams filtered data sectors into the Movie Card", "[mpeg][movie-card][cdblock]") {
    CDBlockHarness h;
    auto image = BuildMode1DiscImage(kTinyMpegProgramStream);
    ConfigureSingleDataTrack(h.disc, std::move(image), 1);

    h.RunCommand(0xE000, 0x0001);
    h.RunCommand(0x9300);
    h.RunCommand(0x9500); // MPEG play
    h.RunCommand(0x9A00, 0x0000); // MPEG connection consumes partition 0
    h.RunCommand(0x9B00);
    CHECK(h.RR(1) == 0x0000);
    h.RunCommand(0x3000, 0x0000, 0x0000, 0x0000); // CD device -> filter 0 -> partition 0
    h.RunCommand(0x3100);
    CHECK((h.RR(2) >> 8u) == 0x00);
    CHECK(h.cdb.GetMPEGCard().GetStatus() == mpeg::MPEGCardStatus::Playing);
    h.RunCommand(0x1080, 150, 0x0080, 10); // play enough FADs to leave seek state and read sector 150

    // First drive tick starts the seek. Subsequent ticks finish seek and read sectors.
    for (int i = 0; i < 20; ++i) {
        h.scheduler.Advance(cdblock::kDriveCyclesPlaying1x / 2);
    }

    h.cdb.GetMPEGCard().SignalEndOfStream();
    h.RunCommand(0x3200);
    CHECK((h.RR(2) >> 8u) == 0x00);
    CHECK(h.cdb.GetMPEGCard().GetWidth() == 16);
    REQUIRE(h.cdb.GetMPEGCard().HasHeaders());
    REQUIRE(h.cdb.GetMPEGCard().DecodeNextFrame());
    REQUIRE(h.cdb.GetMPEGCard().HasCurrentFrame());
    CHECK(h.cdb.GetMPEGCard().GetCurrentFrame().width == 16);
    CHECK(h.cdb.GetMPEGCard().GetCurrentFrame().height == 16);

    h.RunCommand(0x9100);
    CHECK((h.RR(1) & mpeg::kMPEGCardInterruptFrameDecoded) != 0);
}
