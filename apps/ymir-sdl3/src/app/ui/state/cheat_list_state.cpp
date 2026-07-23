#include "cheat_list_state.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace app::ui {

using namespace ymir;

namespace {

// Trim leading/trailing ASCII whitespace.
std::string_view Trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

// Parse a hex integer from the entire view. Returns true on success.
bool ParseHex(std::string_view s, uint32 &out) {
    if (s.empty()) {
        return false;
    }
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
    }
    if (s.empty()) {
        return false;
    }
    uint32 value = 0;
    for (char c : s) {
        uint32 digit;
        if (c >= '0' && c <= '9') {
            digit = static_cast<uint32>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<uint32>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<uint32>(c - 'A' + 10);
        } else {
            return false;
        }
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

} // namespace

bool CheatList::ParseLine(std::string_view line, sys::CheatCode &out) {
    // Strip comments.
    if (auto hash = line.find('#'); hash != std::string_view::npos) {
        line = line.substr(0, hash);
    }
    if (auto semi = line.find(';'); semi != std::string_view::npos) {
        line = line.substr(0, semi);
    }
    line = Trim(line);
    if (line.empty()) {
        return false;
    }

    // Find the boundary between address and value. Accept whitespace, ':' or '=' as separator.
    size_t sep = std::string_view::npos;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ':' || c == '=') {
            sep = i;
            break;
        }
    }
    if (sep == std::string_view::npos) {
        return false;
    }

    std::string_view addrStr = Trim(line.substr(0, sep));
    std::string_view valStr = Trim(line.substr(sep + 1));
    // Skip additional separator characters that may run together.
    while (!valStr.empty() &&
           (std::isspace(static_cast<unsigned char>(valStr.front())) || valStr.front() == ':' || valStr.front() == '=')) {
        valStr.remove_prefix(1);
    }
    if (addrStr.empty() || valStr.empty()) {
        return false;
    }

    uint32 address = 0;
    uint32 value = 0;
    if (!ParseHex(addrStr, address)) {
        return false;
    }
    if (!ParseHex(valStr, value)) {
        return false;
    }

    sys::CheatWidth width;
    if (valStr.size() <= 2) {
        width = sys::CheatWidth::Byte;
    } else if (valStr.size() <= 4) {
        width = sys::CheatWidth::Word;
    } else {
        width = sys::CheatWidth::Long;
    }

    out.address = address;
    out.value = value;
    out.width = width;
    return true;
}

bool CheatList::AddFromText(std::string name, std::string text, bool enabled) {
    CheatEntry entry;
    entry.name = std::move(name);
    entry.rawText = text;
    entry.enabled = enabled;

    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        sys::CheatCode code{};
        if (CheatList::ParseLine(line, code)) {
            entry.codes.push_back(code);
        }
    }

    if (entry.codes.empty()) {
        return false;
    }
    m_entries.push_back(std::move(entry));
    return true;
}

void CheatList::RemoveAt(size_t index) {
    if (index < m_entries.size()) {
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void CheatList::Clear() {
    m_entries.clear();
}

void CheatList::SetEntryEnabled(size_t index, bool enabled) {
    if (index < m_entries.size()) {
        m_entries[index].enabled = enabled;
    }
}

void CheatList::DisableAll() {
    for (auto &e : m_entries) {
        e.enabled = false;
    }
}

std::vector<sys::CheatCode> CheatList::BuildActiveCodes() const {
    std::vector<sys::CheatCode> out;
    if (!m_masterEnabled) {
        return out;
    }
    for (const auto &e : m_entries) {
        if (!e.enabled) {
            continue;
        }
        out.insert(out.end(), e.codes.begin(), e.codes.end());
    }
    return out;
}

} // namespace app::ui
