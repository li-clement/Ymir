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

// Build a single-track mode-1 disc image from 2048-byte user-data sectors.
// Track::ReadSector expects a complete raw sector-sized unit and synthesizes
// the missing mode-1 headers/ECC when the track is declared as 2048 bytes.
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

struct CDBlockHarness {
    core::Scheduler scheduler;
    // CDInterface wraps a Disc via ImageCDDevice; tests construct a disc,
    // load it into m_cdInterface via LoadMode1DiscImage, and CDBlock reads
    // through that interface. TV tests that don't need a disc simply skip
    // the load call.
    media::CDInterface cdif;
    media::fs::Filesystem fs;
    core::Configuration::CDBlock config;
    cdblock::CDBlock cdb{scheduler, cdif, fs, config};
    sys::SH2Bus bus;
    uint32 extInterruptCount = 0;

    CDBlockHarness() {
        cdb.MapCallbacks(
            {this, [](void *ctx) { static_cast<CDBlockHarness *>(ctx)->extInterruptCount++; }},
            {this, [](std::span<uint8, 2352>, void *) -> uint32 { return 0; }});
        cdb.MapMemory(bus);
    }

    // Build a mode-1 disc image from the payload, attach it to the CD
    // interface so the CD block can read sectors from it. sectorCount is
    // rounded up to give the CD block the full FAD range to drive seeking.
    void LoadMode1DiscImage(std::span<const uint8> payload) {
        auto image = BuildMode1DiscImage(payload);
        const uint32 sectorCount = static_cast<uint32>(image.size() / 2048);
        media::Disc disc;
        ConfigureSingleDataTrack(disc, std::move(image), sectorCount);
        cdif.LoadDisc(std::move(disc));
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
    // MpegGetStatus returns 0xFF (card removed) in CR1 to bypass MPEG polling
    // loops for game compatibility (Pitfall #1).
    CHECK((h.HIRQ() & cdblock::kHIRQ_MPED) != 0);

    h.RunCommand(0x9000); // MPEG get status - returns 0xFF00 (card removed)
    CHECK(h.RR(0) == 0xFF00);
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
    CHECK((h.RR(1) & mpeg::kMPEGCardInterruptFrameDecoded) != 0);

    h.RunCommand(0x9200, mpeg::kMPEGCardInterruptFrameDecoded);
    h.RunCommand(0x9100);
    CHECK((h.RR(1) & 0x0001) == 0);
}


TEST_CASE("CD Block playback streams filtered data sectors into the Movie Card", "[mpeg][movie-card][cdblock]") {
    CDBlockHarness h;
    h.LoadMode1DiscImage(kTinyMpegProgramStream);

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

TEST_CASE("CD Block $A1 MpegSetWindow latches the EXBG display window for Moon Cradle",
          "[mpeg][movie-card][cdblock][window]") {
    CDBlockHarness h;

    h.RunCommand(0xE000, 0x0001); // Authenticate MPEG
    h.RunCommand(0x9300);         // MPEG init

    // Frame-buffer position (sub 0): source-side anchor into the Movie Card
    // frame buffer. Moon Cradle's opening FMV uses X=$20, Y=$28.
    // CR1 low byte = sub-index 0, CR2 = X, CR3 = Y.
    h.RunCommand(0xA100, 0x0001, 0x0020, 0x0028);
    CHECK(h.cdb.GetMpegWindowFbPosX() == 0x0020);
    CHECK(h.cdb.GetMpegWindowFbPosY() == 0x0028);

    // Frame-buffer ratio (sub 1): raw wire values, 1:1 encodes as $8011.
    h.RunCommand(0xA101, 0x0001, 0x8011, 0x8011);
    CHECK(h.cdb.GetMpegWindowFbRatioX() == 0x8011);
    CHECK(h.cdb.GetMpegWindowFbRatioY() == 0x8011);

    // Display position (sub 2): signed X/Y in decoder raster coordinates.
    // Moon Cradle's opening FMV centres at X=$0F, Y=$28.
    h.RunCommand(0xA102, 0x0001, 0x000F, 0x0028);
    CHECK(h.cdb.GetMpegWindowDispPosX() == 0x000F);
    CHECK(h.cdb.GetMpegWindowDispPosY() == 0x0028);

    // Display size (sub 3): visible extent. Moon Cradle opening FMV is
    // 288x160 (centre inset of the 320x240 screen).
    h.RunCommand(0xA103, 0x0001, 0x0120, 0x00A0);
    CHECK(h.cdb.GetMpegWindowDispSizeW() == 0x0120);
    CHECK(h.cdb.GetMpegWindowDispSizeH() == 0x00A0);

    // In-game FMV re-sends only the changed sub-parameters (ratio stays 1:1):
    // frame-buffer position $60/$3C, display position $17/$31, size 160x120.
    h.RunCommand(0xA100, 0x0001, 0x0060, 0x003C);
    h.RunCommand(0xA102, 0x0001, 0x0017, 0x0031);
    h.RunCommand(0xA103, 0x0001, 0x00A0, 0x0078);
    CHECK(h.cdb.GetMpegWindowFbPosX() == 0x0060);
    CHECK(h.cdb.GetMpegWindowFbPosY() == 0x003C);
    CHECK(h.cdb.GetMpegWindowFbRatioX() == 0x8011); // unchanged
    CHECK(h.cdb.GetMpegWindowFbRatioY() == 0x8011); // unchanged
    CHECK(h.cdb.GetMpegWindowDispPosX() == 0x0017);
    CHECK(h.cdb.GetMpegWindowDispPosY() == 0x0031);
    CHECK(h.cdb.GetMpegWindowDispSizeW() == 0x00A0);
    CHECK(h.cdb.GetMpegWindowDispSizeH() == 0x0078);

    // MpegInit does not clear the EXBG latches; Moon Cradle initializes
    // once and reuses the window for later movies.
    h.RunCommand(0x9300);
    CHECK(h.cdb.GetMpegWindowDispSizeW() == 0x00A0);
    CHECK(h.cdb.GetMpegWindowDispSizeH() == 0x0078);
}

TEST_CASE("CD Block $A2/$A3/$A4 MpegSet{Border,Fade,VideoEffects} latch without errors",
          "[mpeg][movie-card][cdblock][window]") {
    CDBlockHarness h;

    h.RunCommand(0xE000, 0x0001);
    h.RunCommand(0x9300);

    // Border colour: $0000 (black).
    h.RunCommand(0xA200, 0x0000, 0x0000, 0x0000);
    CHECK((h.HIRQ() & cdblock::kHIRQ_CMOK) != 0);

    // Fade: $0000 (no fade).
    h.RunCommand(0xA300, 0x0000, 0x0000, 0x0000);
    CHECK((h.HIRQ() & cdblock::kHIRQ_CMOK) != 0);

    // Video effects: Moon Cradle sends $0F00 on every FMV (interpolation flags).
    h.RunCommand(0xA400, 0x0F00, 0x0000, 0x0000);
    CHECK((h.HIRQ() & cdblock::kHIRQ_CMOK) != 0);
}
