#pragma once

/**
@file
@brief Memory searcher (Cheat Engine-style) for locating cheat addresses.

Workflow:

  1. The user names an in-game value they want to manipulate (e.g. money).
  2. Call FirstScan(op, width, value) — scans Low/High WRAM and records every
     address whose current value matches.
  3. The user changes the value in-game, then calls NextScan(op, value) — the
     existing match list is filtered down.
  4. Iterate steps 3 until the list is small enough to add the surviving
     candidate(s) to the CheatEngine.

We deliberately read the WRAM arrays directly (not via SH2Bus::Peek) because
the bus path is too slow for a 2 MiB sweep — direct memory iteration runs
about two orders of magnitude faster.

Endianness: Saturn is big-endian. Word/long reads use big-endian decode by
default; toggle endian=false on the search to read little-endian instead
(rare, but some games use packed structures via the SH-2 R-class).
*/

#include <ymir/core/types.hpp>

#include <vector>

namespace ymir::sys {

struct SystemMemory;

/// @brief Width of each scanned value.
enum class SearchWidth : uint8 {
    Byte = 1, ///< 8-bit
    Word = 2, ///< 16-bit
    Long = 4, ///< 32-bit
};

/// @brief How a candidate value is compared against the search parameter.
enum class SearchOp : uint8 {
    Equal,            ///< value == operand
    NotEqual,         ///< value != operand
    Greater,          ///< value > operand
    Less,             ///< value < operand
    GreaterThanLast,  ///< value > previous-scan value
    LessThanLast,     ///< value < previous-scan value
    EqualToLast,      ///< value == previous-scan value (unchanged)
    NotEqualToLast,   ///< value != previous-scan value (changed)
    IncreasedBy,      ///< value - previous == operand
    DecreasedBy,      ///< previous - value == operand
};

/// @brief A single address that survived the most recent scan.
struct SearchMatch {
    uint32 address;   ///< SH-2 bus address (0x00200000+ Low WRAM or 0x06000000+ High WRAM)
    uint32 lastValue; ///< Value observed at the most recent scan
};

/// @brief Memory searcher. Reads directly from SystemMemory WRAM banks.
///
/// Not thread-safe. Intended to be driven from the UI thread between frames.
class CheatSearch {
public:
    CheatSearch() = default;

    /// @brief Resets the search to the empty state (no scan performed).
    void Reset();

    /// @brief Whether a first scan has been performed.
    bool HasInitialScan() const { return m_hasInitialScan; }

    /// @brief Number of candidate addresses currently surviving.
    size_t MatchCount() const { return m_matches.size(); }

    /// @brief Current search width.
    SearchWidth Width() const { return m_width; }

    /// @brief Read-only access to surviving matches.
    const std::vector<SearchMatch> &Matches() const { return m_matches; }

    /// @brief Whether the active search is reading big-endian values.
    bool BigEndian() const { return m_bigEndian; }
    void SetBigEndian(bool be) { m_bigEndian = be; }

    /// @brief Performs the first scan against the entire low+high WRAM.
    /// @param[in] mem  the system memory to read from
    /// @param[in] op   comparison operator
    /// @param[in] width access width
    /// @param[in] operand the comparison operand (ignored for unchanged/changed/etc.)
    void FirstScan(const SystemMemory &mem, SearchOp op, SearchWidth width, uint32 operand);

    /// @brief Refines the search by filtering the current match list.
    /// @param[in] mem  the system memory to read from (re-read at current state)
    /// @param[in] op   comparison operator
    /// @param[in] operand the comparison operand
    void NextScan(const SystemMemory &mem, SearchOp op, uint32 operand);

    /// @brief Re-reads current values for all surviving matches without filtering.
    /// Useful before showing the table when the game is running.
    void RefreshValues(const SystemMemory &mem);

private:
    std::vector<SearchMatch> m_matches;
    SearchWidth m_width = SearchWidth::Word;
    bool m_bigEndian = true;
    bool m_hasInitialScan = false;
};

} // namespace ymir::sys
