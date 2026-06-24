#pragma once

#include <app/ui/state/cheat_list_state.hpp>
#include <app/ui/window_base.hpp>

#include <ymir/sys/cheat_search.hpp>

#include <array>
#include <string>

namespace app::ui {

/// @brief Cheat code manager + Cheat-Engine-style memory searcher.
///
/// Owns the frontend cheat list (CheatList). On every mutation we recompute
/// the active patch list and push it to the emulator via
/// events::emu::SetActiveCheatCodes — the emulator core never sees names,
/// raw text, or disabled cheats, only the flat patch list it applies each
/// frame.
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

    /// @brief Recomputes the active flat patch list from m_cheatList and
    /// enqueues SetActiveCheatCodes. Call after every UI mutation.
    void PushActiveCodes();

    // ---- Cheat list (frontend source of truth) ------------------------------
    CheatList m_cheatList;

    // ---- Searcher state (UI-only, no emulator round-trip) -------------------
    ymir::sys::CheatSearch m_search;
    int m_searchWidth = 1;          ///< 0=Byte, 1=Word, 2=Long
    int m_searchOp = 0;             ///< Index into the operator table in .cpp
    std::array<char, 32> m_operand{}; ///< Hex operand text buffer
    bool m_autoRefresh = true;

    // ---- Add-cheat form -----------------------------------------------------
    std::array<char, 64> m_newName{};
    std::array<char, 4096> m_newCode{};
    std::string m_lastError;

    // ---- "Freeze at" override used by per-row Freeze buttons ----------------
    std::array<char, 32> m_freezeValue{};
    bool m_freezeEnabledOnAdd = true;
};

} // namespace app::ui
