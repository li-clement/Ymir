#include <ymir/db/game_db.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace ymir;

TEST_CASE("Game database recognises Lunar SS Story MPEG as a Movie Card title",
          "[game-db][movie-card]") {
    const auto *info = db::GetGameInfo("T-27904G", {});
    REQUIRE(info != nullptr);
    CHECK(BitmaskEnum(info->flags).AnyOf(db::GameInfo::Flags::MovieCard));
}

TEST_CASE("Game database recognises Vatlva as a Movie Card title",
          "[game-db][movie-card]") {
    const auto *info = db::GetGameInfo("T-31501G", {});
    REQUIRE(info != nullptr);
    CHECK(BitmaskEnum(info->flags).AnyOf(db::GameInfo::Flags::MovieCard));
}

TEST_CASE("Game database recognises Moon Cradle as a Movie Card title",
          "[game-db][movie-card][moon-cradle]") {
    const auto *info = db::GetGameInfo("T-9109G", {});
    REQUIRE(info != nullptr);
    CHECK(BitmaskEnum(info->flags).AnyOf(db::GameInfo::Flags::MovieCard));
}
