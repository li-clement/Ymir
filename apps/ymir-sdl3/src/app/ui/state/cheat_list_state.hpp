#pragma once

/**
@file
@brief Frontend-side cheat list state.

The emulator core (libymir-core's CheatEngine) is a dumb per-frame memory
patcher — it only knows about (address, value, width) tuples and how to
apply them via SH2Bus::Poke. Everything else — names, raw code text, the
notion of multi-line "cheat entries" that may be enabled or disabled
independently, the parser for Action Replay RAW-format codes — lives here,
in the frontend.

When the user mutates the list (add, remove, toggle, master-enable), the
frontend rebuilds the flat list of currently-active CheatCode patches and
pushes it to the emulator via events::emu::SetActiveCheatCodes(). The
emulator side never sees names, presentation order, or disabled cheats.
*/

#include <ymir/sys/cheats.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace app::ui {

/// @brief One named cheat entry in the frontend's list.
///
/// A single entry can hold multiple `CheatCode` writes that apply together
/// when enabled (typical for multi-line Action Replay codes).
struct CheatEntry {
    std::string name;                          ///< User-supplied label
    std::string rawText;                       ///< Original code text, kept for display/editing
    std::vector<ymir::sys::CheatCode> codes;   ///< Parsed patches
    bool enabled = false;                      ///< Whether to include in the active flat list
};

/// @brief Frontend cheat list: user-facing data model + parsing.
///
/// The CheatList is the single source of truth for what cheats exist; the
/// emulator-side `CheatEngine` is a downstream consumer that only receives
/// the currently-active flat patch list.
class CheatList {
public:
    CheatList() = default;

    // ---- Mutation API (UI-only — emulator state is updated via events) ----

    /// @brief Parses a multi-line text block and appends a new entry.
    /// @return `true` if at least one valid code line was parsed
    bool AddFromText(std::string name, std::string text, bool enabled = false);

    /// @brief Removes the entry at the given index. No-op if out of range.
    void RemoveAt(size_t index);

    /// @brief Clears all entries.
    void Clear();

    /// @brief Sets the enabled flag on the entry at the given index.
    void SetEntryEnabled(size_t index, bool enabled);

    /// @brief Sets every entry's enabled flag to `false`.
    void DisableAll();

    /// @brief Master enable: when false, BuildActiveCodes() returns empty
    /// regardless of per-entry state.
    bool MasterEnabled() const { return m_masterEnabled; }
    void SetMasterEnabled(bool enabled) { m_masterEnabled = enabled; }

    // ---- Inspection ----

    size_t Count() const { return m_entries.size(); }
    std::vector<CheatEntry> &Entries() { return m_entries; }
    const std::vector<CheatEntry> &Entries() const { return m_entries; }

    /// @brief Computes the flat list of currently-active patches.
    ///
    /// Returns empty if master is disabled. Otherwise collects codes from
    /// every entry whose `enabled` is true, in entry order.
    std::vector<ymir::sys::CheatCode> BuildActiveCodes() const;

    // ---- Parsing ----

    /// @brief Parses one line of raw cheat text. See CheatList::AddFromText
    /// for syntax. Returns false on parse error.
    static bool ParseLine(std::string_view line, ymir::sys::CheatCode &out);

private:
    std::vector<CheatEntry> m_entries;
    bool m_masterEnabled = true;
};

} // namespace app::ui
