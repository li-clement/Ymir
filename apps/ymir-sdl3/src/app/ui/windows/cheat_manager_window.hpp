#pragma once

#include <app/ui/window_base.hpp>

#include <ymir/sys/cheat_search.hpp>

#include <array>
#include <string>

namespace app::ui {

/// @brief Cheat code manager + Cheat-Engine-style memory searcher.
///
/// Top half: searcher (find addresses by repeatedly narrowing on observed
/// in-game value changes). Bottom half: the list of cheat codes the engine
/// applies each frame.
class CheatManagerWindow : public WindowBase {
public:
    CheatManagerWindow(SharedContext &context);

protected:
    void PrepareWindow() final;
    void DrawContents() final;

private:
    void DrawSearchSection();
    void DrawAddSection();
    void DrawCheatList();

    // ---- Searcher state -----------------------------------------------------
    ymir::sys::CheatSearch m_search;
    int m_searchWidth = 1;          ///< 0=Byte, 1=Word, 2=Long
    int m_searchOp = 0;             ///< Indexes into kSearchOps in the .cpp
    std::array<char, 32> m_operand{}; ///< Hex operand text buffer
    bool m_autoRefresh = true;      ///< Re-read current values on every draw

    // ---- Add-cheat form -----------------------------------------------------
    std::array<char, 64> m_newName{};
    std::array<char, 4096> m_newCode{};
    std::string m_lastError;

    // Optional freeze-value override used when "Freeze" is clicked in the
    // search results. Empty = freeze with the current observed value.
    std::array<char, 32> m_freezeValue{};
    bool m_freezeEnabledOnAdd = true; ///< Auto-enable cheats added from search
};

} // namespace app::ui
