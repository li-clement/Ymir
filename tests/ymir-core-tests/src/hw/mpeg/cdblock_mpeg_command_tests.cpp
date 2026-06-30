#include <ymir/hw/cdblock/cdblock.hpp>
#include <ymir/hw/cdblock/cdblock_defs.hpp>
#include <ymir/media/disc.hpp>
#include <ymir/media/filesystem.hpp>
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
