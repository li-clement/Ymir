#include <ymir/sys/cheat_search.hpp>

#include <ymir/sys/memory.hpp>

#include <algorithm>
#include <cstring>

namespace ymir::sys {

namespace {

// Saturn SH-2 bus base addresses for the two main 1 MiB WRAM banks.
// (These are the cached aliases; we don't bother enumerating the mirrors.)
constexpr uint32 kWRAMLowBase = 0x00200000u;
constexpr uint32 kWRAMHighBase = 0x06000000u;

// Reads `width` bytes from `buffer` starting at `offset`, decoding into a
// uint32. Buffer is assumed to be big-endian Saturn memory; flip if requested.
inline uint32 ReadValue(const uint8 *buffer, size_t offset, SearchWidth width, bool bigEndian) {
    switch (width) {
    case SearchWidth::Byte: //
        return buffer[offset];
    case SearchWidth::Word: {
        uint32 hi = buffer[offset];
        uint32 lo = buffer[offset + 1];
        return bigEndian ? ((hi << 8) | lo) : ((lo << 8) | hi);
    }
    case SearchWidth::Long: {
        uint32 b0 = buffer[offset];
        uint32 b1 = buffer[offset + 1];
        uint32 b2 = buffer[offset + 2];
        uint32 b3 = buffer[offset + 3];
        return bigEndian ? ((b0 << 24) | (b1 << 16) | (b2 << 8) | b3)
                         : ((b3 << 24) | (b2 << 16) | (b1 << 8) | b0);
    }
    }
    return 0;
}

// Reads the current value at the given SH-2 bus address. Returns 0 if the
// address falls outside the two WRAM banks (shouldn't happen by construction).
inline uint32 ReadAtBusAddress(const SystemMemory &mem, uint32 address, SearchWidth width, bool bigEndian) {
    if (address >= kWRAMHighBase && address + static_cast<uint32>(width) <= kWRAMHighBase + mem.WRAMHigh.size()) {
        return ReadValue(mem.WRAMHigh.data(), address - kWRAMHighBase, width, bigEndian);
    }
    if (address >= kWRAMLowBase && address + static_cast<uint32>(width) <= kWRAMLowBase + mem.WRAMLow.size()) {
        return ReadValue(mem.WRAMLow.data(), address - kWRAMLowBase, width, bigEndian);
    }
    return 0;
}

inline bool Compare(SearchOp op, uint32 cur, uint32 last, uint32 operand, SearchWidth width) {
    // For Less / Greater / IncreasedBy / DecreasedBy on signed-feeling values
    // we still compare as unsigned; the user can pick the right operand value
    // for the width (e.g. 0xFFFF for -1 on a 16-bit field).
    const uint32 mask = (width == SearchWidth::Byte)   ? 0xFFu
                        : (width == SearchWidth::Word) ? 0xFFFFu
                                                       : 0xFFFFFFFFu;
    cur &= mask;
    last &= mask;
    operand &= mask;

    switch (op) {
    case SearchOp::Equal:           return cur == operand;
    case SearchOp::NotEqual:        return cur != operand;
    case SearchOp::Greater:         return cur > operand;
    case SearchOp::Less:            return cur < operand;
    case SearchOp::GreaterThanLast: return cur > last;
    case SearchOp::LessThanLast:    return cur < last;
    case SearchOp::EqualToLast:     return cur == last;
    case SearchOp::NotEqualToLast:  return cur != last;
    case SearchOp::IncreasedBy:     return (cur - last) == operand;
    case SearchOp::DecreasedBy:     return (last - cur) == operand;
    }
    return false;
}

// Walks one WRAM bank and appends matches to `out`. Step size equals width
// so we don't emit overlapping matches.
void ScanBank(const uint8 *bank, size_t bankSize, uint32 busBase, SearchOp op, SearchWidth width, bool bigEndian,
              uint32 operand, std::vector<SearchMatch> &out) {
    const size_t step = static_cast<size_t>(width);
    if (step == 0 || bankSize < step) {
        return;
    }
    const size_t end = bankSize - step + 1;
    for (size_t i = 0; i < end; i += step) {
        const uint32 cur = ReadValue(bank, i, width, bigEndian);
        // For ops that compare against "last", first scan treats last == cur,
        // so EqualToLast keeps everything and NotEqualToLast keeps nothing.
        // We just feed cur as last; Compare() handles the rest.
        if (Compare(op, cur, cur, operand, width)) {
            out.push_back(SearchMatch{busBase + static_cast<uint32>(i), cur});
        }
    }
}

} // namespace

void CheatSearch::Reset() {
    m_matches.clear();
    m_hasInitialScan = false;
}

void CheatSearch::FirstScan(const SystemMemory &mem, SearchOp op, SearchWidth width, uint32 operand) {
    m_matches.clear();
    m_width = width;
    m_matches.reserve(1024);

    ScanBank(mem.WRAMLow.data(), mem.WRAMLow.size(), kWRAMLowBase, op, width, m_bigEndian, operand, m_matches);
    ScanBank(mem.WRAMHigh.data(), mem.WRAMHigh.size(), kWRAMHighBase, op, width, m_bigEndian, operand, m_matches);

    m_hasInitialScan = true;
}

void CheatSearch::NextScan(const SystemMemory &mem, SearchOp op, uint32 operand) {
    if (!m_hasInitialScan) {
        return;
    }
    // Re-read each surviving address at the current width and keep only those
    // that still satisfy the new predicate. Update lastValue along the way so
    // *ThanLast operators in subsequent scans use the freshly observed value.
    auto pred = [&](SearchMatch &m) {
        const uint32 cur = ReadAtBusAddress(mem, m.address, m_width, m_bigEndian);
        const bool keep = Compare(op, cur, m.lastValue, operand, m_width);
        if (keep) {
            m.lastValue = cur;
        }
        return !keep;
    };

    m_matches.erase(std::remove_if(m_matches.begin(), m_matches.end(), pred), m_matches.end());
}

void CheatSearch::RefreshValues(const SystemMemory &mem) {
    for (auto &m : m_matches) {
        m.lastValue = ReadAtBusAddress(mem, m.address, m_width, m_bigEndian);
    }
}

} // namespace ymir::sys
