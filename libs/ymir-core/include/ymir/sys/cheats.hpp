#pragma once

/**
@file
@brief Cheat code engine (Action Replay / Pro Action Replay style RAW codes).

The engine stores a list of user-supplied codes that patch SH-2 bus memory
each frame before emulation runs. Codes use the simple format:

    AAAAAAAA VVVV   (32-bit address, 16-bit value)
    AAAAAAAA VV     (32-bit address, 8-bit value)
    AAAAAAAA LLLLLLLL (32-bit address, 32-bit value -- two writes packed)

Comments start with '#' or ';'. Blank lines and whitespace are ignored.
Address ranges are not validated -- writes go through SH2Bus::Poke() which
silently drops writes to unmapped regions. Typical safe targets:

    0x00200000 - 0x002FFFFF  Low WRAM  (1 MiB)
    0x06000000 - 0x060FFFFF  High WRAM (1 MiB)

These are mirrors at the Saturn cached address range. Cartridge backup RAM
sits at 0x02000000+ and is also a valid target.

This is the minimal "RAW code" engine. Action Replay / Game Genie token
encodings are NOT decoded here -- users supply pre-decoded address/value
pairs. A future enhancement could add a decoder.
*/

#include <ymir/core/types.hpp>

#include <ymir/sys/bus.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace ymir::sys {

/// @brief Width of a single cheat code write.
enum class CheatWidth : uint8 {
    Byte = 1,  ///< 8-bit write
    Word = 2,  ///< 16-bit write
    Long = 4,  ///< 32-bit write
};

/// @brief A single cheat code line: address + value + width.
struct CheatCode {
    uint32 address = 0;
    uint32 value = 0;
    CheatWidth width = CheatWidth::Word;
};

/// @brief A named, optionally-enabled group of cheat codes.
///
/// Each entry corresponds to one logical cheat (e.g. "Infinite Lives") which
/// may consist of one or more `CheatCode` lines applied together.
struct CheatEntry {
    std::string name;             ///< User-visible label
    std::string rawText;          ///< Original code text, preserved for the UI
    std::vector<CheatCode> codes; ///< Decoded codes
    bool enabled = false;         ///< Whether this entry is currently active
};

/// @brief Cheat code engine. Owned by Saturn; applied each frame.
///
/// Not thread-safe. The engine is intended to be touched only from the
/// emulation thread (between frames) and from the UI thread while emulation
/// is paused or under the emulator's standard input event queue model.
class CheatEngine {
public:
    CheatEngine() = default;

    /// @brief Removes all cheat entries.
    void Clear();

    /// @brief Number of cheat entries currently held.
    size_t Count() const { return m_entries.size(); }

    /// @brief Master enable: when false, ApplyAll() does nothing regardless of per-entry state.
    bool MasterEnabled() const { return m_masterEnabled; }
    void SetMasterEnabled(bool enabled) { m_masterEnabled = enabled; }

    /// @brief Mutable access to entries (for UI editing).
    std::vector<CheatEntry> &Entries() { return m_entries; }
    const std::vector<CheatEntry> &Entries() const { return m_entries; }

    /// @brief Parses a raw multi-line code text and creates a new entry.
    ///
    /// @param[in] name human-readable label
    /// @param[in] text multi-line text containing one code per line
    /// @param[in] enabled initial enabled state
    /// @return `true` if at least one valid code line was parsed
    bool AddFromText(std::string name, std::string text, bool enabled = false);

    /// @brief Parses one line of raw cheat text.
    ///
    /// Format examples:
    ///   `060FFFF8 0009`    -> word write of 0x0009 at 0x060FFFF8
    ///   `060FFFF8:09`      -> byte write of 0x09 at 0x060FFFF8
    ///   `060FFFF8 12345678`-> long write
    ///
    /// Whitespace, ':' and '=' are accepted as separators. Returns false on
    /// any parse error (caller may then surface a UI message).
    static bool ParseLine(std::string_view line, CheatCode &out);

    /// @brief Applies all enabled cheats to the bus.
    ///
    /// Called by Saturn::RunFrameImpl() at the start of every frame so codes
    /// that the game keeps clobbering get re-applied promptly.
    void ApplyAll(SH2Bus &bus) const;

    /// @brief Removes the entry at the given index. No-op if out of range.
    void RemoveAt(size_t index);

private:
    std::vector<CheatEntry> m_entries;
    bool m_masterEnabled = true;
};

} // namespace ymir::sys
