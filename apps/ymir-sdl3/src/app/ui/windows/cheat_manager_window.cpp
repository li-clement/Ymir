#include "cheat_manager_window.hpp"

#include <ymir/sys/cheats.hpp>
#include <ymir/sys/saturn.hpp>

#include <imgui.h>

#include <cstdio>
#include <cstring>

using namespace ymir;

namespace app::ui {

namespace {

constexpr const char *kWidthLabels[] = {"8-bit", "16-bit", "32-bit"};

struct SearchOpEntry {
    const char *label;
    sys::SearchOp op;
    bool needsOperand;
};

// Order chosen for ease of use: most common ops first.
constexpr SearchOpEntry kSearchOps[] = {
    {"= value",          sys::SearchOp::Equal,           true},
    {"!= value",         sys::SearchOp::NotEqual,        true},
    {"> value",          sys::SearchOp::Greater,         true},
    {"< value",          sys::SearchOp::Less,            true},
    {"unchanged",        sys::SearchOp::EqualToLast,     false},
    {"changed",          sys::SearchOp::NotEqualToLast,  false},
    {"> last",           sys::SearchOp::GreaterThanLast, false},
    {"< last",           sys::SearchOp::LessThanLast,    false},
    {"increased by N",   sys::SearchOp::IncreasedBy,     true},
    {"decreased by N",   sys::SearchOp::DecreasedBy,     true},
};

// Parses a hex operand. Returns 0 on parse failure (the UI gates this).
uint32 ParseHex(const char *s) {
    if (s == nullptr || *s == '\0') {
        return 0;
    }
    uint32 v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    for (; *s; ++s) {
        char c = *s;
        uint32 d;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10;
        } else {
            return v; // bail on first non-hex char
        }
        v = (v << 4) | d;
    }
    return v;
}

sys::SearchWidth ToWidth(int idx) {
    switch (idx) {
    case 0: return sys::SearchWidth::Byte;
    case 1: return sys::SearchWidth::Word;
    default: return sys::SearchWidth::Long;
    }
}

int DigitsForWidth(sys::SearchWidth w) {
    switch (w) {
    case sys::SearchWidth::Byte: return 2;
    case sys::SearchWidth::Word: return 4;
    case sys::SearchWidth::Long: return 8;
    }
    return 4;
}

const char *WidthName(sys::SearchWidth w) {
    switch (w) {
    case sys::SearchWidth::Byte: return "8 ";
    case sys::SearchWidth::Word: return "16";
    case sys::SearchWidth::Long: return "32";
    }
    return "??";
}

} // namespace

CheatManagerWindow::CheatManagerWindow(SharedContext &context)
    : WindowBase(context) {
    m_windowConfig.name = "Cheat manager";
}

void CheatManagerWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f * m_context.displayScale, 480.0f * m_context.displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void CheatManagerWindow::DrawContents() {
    auto *saturn = m_context.saturn.instance.get();
    if (saturn == nullptr) {
        ImGui::TextUnformatted("Saturn instance not available.");
        return;
    }

    if (ImGui::CollapsingHeader("Memory searcher", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawSearchSection();
    }
    if (ImGui::CollapsingHeader("Active cheats", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawCheatList();
    }
    if (ImGui::CollapsingHeader("Add cheat manually")) {
        DrawAddSection();
    }
}

void CheatManagerWindow::DrawSearchSection() {
    auto *saturn = m_context.saturn.instance.get();
    auto &mem = saturn->mem;
    auto &engine = saturn->cheats;

    ImGui::TextDisabled(
        "Workflow: pick a value width, scan for the value, change it in-game, then narrow with the buttons below.");

    // --- Width + operator + operand row ---
    ImGui::SetNextItemWidth(90.0f * m_context.displayScale);
    if (ImGui::Combo("Width##search_w", &m_searchWidth, kWidthLabels, IM_ARRAYSIZE(kWidthLabels))) {
        // Width change invalidates the current search (different alignment).
        m_search.Reset();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f * m_context.displayScale);
    if (ImGui::BeginCombo("Compare##search_op", kSearchOps[m_searchOp].label)) {
        for (int i = 0; i < IM_ARRAYSIZE(kSearchOps); ++i) {
            if (ImGui::Selectable(kSearchOps[i].label, m_searchOp == i)) {
                m_searchOp = i;
            }
        }
        ImGui::EndCombo();
    }

    const bool needsOperand = kSearchOps[m_searchOp].needsOperand;
    ImGui::SameLine();
    if (!needsOperand) {
        ImGui::BeginDisabled();
    }
    ImGui::SetNextItemWidth(120.0f * m_context.displayScale);
    ImGui::InputTextWithHint("##operand", "value (hex)", m_operand.data(), m_operand.size(),
                             ImGuiInputTextFlags_CharsHexadecimal);
    if (!needsOperand) {
        ImGui::EndDisabled();
    }

    // --- Scan buttons ---
    const uint32 operand = needsOperand ? ParseHex(m_operand.data()) : 0u;
    const auto op = kSearchOps[m_searchOp].op;

    if (ImGui::Button("First scan")) {
        m_search.FirstScan(mem, op, ToWidth(m_searchWidth), operand);
    }
    ImGui::SameLine();
    if (!m_search.HasInitialScan()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Next scan")) {
        m_search.NextScan(mem, op, operand);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh values")) {
        m_search.RefreshValues(mem);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        m_search.Reset();
    }
    if (!m_search.HasInitialScan()) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-refresh values", &m_autoRefresh);

    // Live-refresh while drawing so the user can watch values change.
    if (m_autoRefresh && m_search.HasInitialScan()) {
        m_search.RefreshValues(mem);
    }

    // --- Results summary + table ---
    if (!m_search.HasInitialScan()) {
        ImGui::TextDisabled("No scan performed yet.");
        return;
    }

    const size_t matchCount = m_search.MatchCount();
    if (matchCount == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                           "No matches. Reset and try again with different criteria.");
        return;
    }
    ImGui::Text("Matches: %zu", matchCount);

    // Render at most kMaxRows to keep the UI responsive on huge initial scans.
    constexpr size_t kMaxRows = 200;
    const size_t rows = matchCount > kMaxRows ? kMaxRows : matchCount;
    if (matchCount > kMaxRows) {
        ImGui::SameLine();
        ImGui::TextDisabled("(showing first %zu — narrow further to see all)", kMaxRows);
    }

    // Freeze-value override row: when set, "Freeze" buttons use this value
    // instead of the currently observed one. Empty = use observed value.
    ImGui::SetNextItemWidth(120.0f * m_context.displayScale);
    ImGui::InputTextWithHint("Freeze at##fv", "<observed>", m_freezeValue.data(), m_freezeValue.size(),
                             ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    ImGui::Checkbox("Enable on add", &m_freezeEnabledOnAdd);
    ImGui::SameLine();
    ImGui::TextDisabled("(hex; leave blank to lock the current value)");

    if (ImGui::BeginTable("matches", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, 150.0f * m_context.displayScale))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.0f * m_context.displayScale);
        ImGui::TableHeadersRow();

        const auto width = m_search.Width();
        const int digits = DigitsForWidth(width);
        const char *widthName = WidthName(width);
        const auto &matches = m_search.Matches();

        for (size_t i = 0; i < rows; ++i) {
            const auto &m = matches[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%08X", m.address);

            ImGui::TableNextColumn();
            ImGui::Text("%0*X  [%s]", digits, m.lastValue, widthName);

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Freeze")) {
                // Use the user-supplied freeze value if non-empty, else the
                // currently observed value.
                const uint32 freezeVal =
                    m_freezeValue[0] != '\0' ? ParseHex(m_freezeValue.data()) : m.lastValue;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%08X %0*X", m.address, digits, freezeVal);
                char name[64];
                std::snprintf(name, sizeof(name), "Freeze %08X = %0*X", m.address, digits, freezeVal);
                engine.AddFromText(name, buf, /*enabled=*/m_freezeEnabledOnAdd);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void CheatManagerWindow::DrawCheatList() {
    auto *saturn = m_context.saturn.instance.get();
    auto &engine = saturn->cheats;

    // Master enable + entry count
    bool master = engine.MasterEnabled();
    if (ImGui::Checkbox("Master enable", &master)) {
        engine.SetMasterEnabled(master);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu cheat%s loaded)", engine.Count(), engine.Count() == 1 ? "" : "s");
    ImGui::SameLine();
    if (ImGui::SmallButton("Disable all")) {
        for (auto &e : engine.Entries()) {
            e.enabled = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear list")) {
        engine.Clear();
    }

    auto &entries = engine.Entries();
    size_t removeIndex = static_cast<size_t>(-1);

    if (entries.empty()) {
        ImGui::TextDisabled("No cheats. Use the searcher above or add manually below.");
        return;
    }

    if (ImGui::BeginTable("cheat_list", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 32.0f * m_context.displayScale);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Codes", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.0f * m_context.displayScale);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < entries.size(); ++i) {
            auto &entry = entries[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Checkbox("##en", &entry.enabled);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.name.empty() ? "(unnamed)" : entry.name.c_str());

            ImGui::TableNextColumn();
            for (const auto &c : entry.codes) {
                const char *widthStr = c.width == sys::CheatWidth::Byte   ? "8 "
                                       : c.width == sys::CheatWidth::Word ? "16"
                                                                          : "32";
                ImGui::Text("%08X = %0*X  [%s]", c.address,
                            c.width == sys::CheatWidth::Byte   ? 2
                            : c.width == sys::CheatWidth::Word ? 4
                                                               : 8,
                            c.value, widthStr);
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Remove")) {
                removeIndex = i;
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (removeIndex != static_cast<size_t>(-1)) {
        engine.RemoveAt(removeIndex);
    }
}

void CheatManagerWindow::DrawAddSection() {
    auto *saturn = m_context.saturn.instance.get();
    auto &engine = saturn->cheats;

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##name", "Name (e.g. Infinite Lives)", m_newName.data(), m_newName.size());

    ImGui::TextDisabled("One code per line: ADDR VALUE (hex). Width inferred from value digits.");
    ImGui::TextDisabled("Examples:  060FFFF8 0009    060FFFF8:09    060FFFF8 12345678");
    ImGui::InputTextMultiline("##codes", m_newCode.data(), m_newCode.size(),
                              ImVec2(-1, 80.0f * m_context.displayScale));

    if (ImGui::Button("Add")) {
        std::string name = m_newName.data();
        std::string text = m_newCode.data();
        if (text.empty()) {
            m_lastError = "Cannot add cheat: code text is empty.";
        } else if (!engine.AddFromText(name.empty() ? "Unnamed" : name, text, /*enabled=*/false)) {
            m_lastError = "No valid code lines found. Each line should be: ADDR VALUE (hex).";
        } else {
            m_lastError.clear();
            m_newName.fill('\0');
            m_newCode.fill('\0');
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear form")) {
        m_newName.fill('\0');
        m_newCode.fill('\0');
        m_lastError.clear();
    }

    if (!m_lastError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_lastError.c_str());
    }
}

} // namespace app::ui
