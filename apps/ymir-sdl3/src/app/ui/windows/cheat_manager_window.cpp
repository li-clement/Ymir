#include "cheat_manager_window.hpp"

#include <app/events/emu_event_factory.hpp>

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
            return v;
        }
        v = (v << 4) | d;
    }
    return v;
}

sys::SearchWidth ToSearchWidth(int idx) {
    switch (idx) {
    case 0: return sys::SearchWidth::Byte;
    case 1: return sys::SearchWidth::Word;
    default: return sys::SearchWidth::Long;
    }
}

int DigitsForSearchWidth(sys::SearchWidth w) {
    switch (w) {
    case sys::SearchWidth::Byte: return 2;
    case sys::SearchWidth::Word: return 4;
    case sys::SearchWidth::Long: return 8;
    }
    return 4;
}

const char *SearchWidthName(sys::SearchWidth w) {
    switch (w) {
    case sys::SearchWidth::Byte: return "8 ";
    case sys::SearchWidth::Word: return "16";
    case sys::SearchWidth::Long: return "32";
    }
    return "??";
}

int DigitsForCheatWidth(sys::CheatWidth w) {
    switch (w) {
    case sys::CheatWidth::Byte: return 2;
    case sys::CheatWidth::Word: return 4;
    case sys::CheatWidth::Long: return 8;
    }
    return 4;
}

const char *CheatWidthName(sys::CheatWidth w) {
    switch (w) {
    case sys::CheatWidth::Byte: return "8 ";
    case sys::CheatWidth::Word: return "16";
    case sys::CheatWidth::Long: return "32";
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

void CheatManagerWindow::PushActiveCodes() {
    m_context.EnqueueEvent(events::emu::SetActiveCheatCodes(m_cheatList.BuildActiveCodes()));
}

void CheatManagerWindow::DrawContents() {
    if (m_context.saturn.instance == nullptr) {
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
    // The search algorithm lives in ymir-core because it encodes Saturn WRAM
    // address aliases and endian rules, but the search object itself is owned
    // by this frontend window. This section only performs read-only
    // debugger/probe-style access (same precedent as Memory Viewer reads);
    // every mutation still flows through events::emu.
    auto &mem = m_context.saturn.instance->mem;

    ImGui::TextDisabled(
        "Workflow: pick a value width, scan for the value, change it in-game, then narrow with the buttons below.");

    ImGui::SetNextItemWidth(90.0f * m_context.displayScale);
    if (ImGui::Combo("Width##search_w", &m_searchWidth, kWidthLabels, IM_ARRAYSIZE(kWidthLabels))) {
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

    const uint32 operand = needsOperand ? ParseHex(m_operand.data()) : 0u;
    const auto op = kSearchOps[m_searchOp].op;

    if (ImGui::Button("First scan")) {
        m_search.FirstScan(mem, op, ToSearchWidth(m_searchWidth), operand);
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

    if (m_autoRefresh && m_search.HasInitialScan()) {
        m_search.RefreshValues(mem);
    }

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

    constexpr size_t kMaxRows = 200;
    const size_t rows = matchCount > kMaxRows ? kMaxRows : matchCount;
    if (matchCount > kMaxRows) {
        ImGui::SameLine();
        ImGui::TextDisabled("(showing first %zu — narrow further to see all)", kMaxRows);
    }

    // Freeze-value override row.
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
        const int digits = DigitsForSearchWidth(width);
        const char *widthName = SearchWidthName(width);
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
                const uint32 freezeVal =
                    m_freezeValue[0] != '\0' ? ParseHex(m_freezeValue.data()) : m.lastValue;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%08X %0*X", m.address, digits, freezeVal);
                char name[64];
                std::snprintf(name, sizeof(name), "Freeze %08X = %0*X", m.address, digits, freezeVal);
                if (m_cheatList.AddFromText(name, buf, /*enabled=*/m_freezeEnabledOnAdd)) {
                    PushActiveCodes();
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void CheatManagerWindow::DrawCheatList() {
    bool master = m_cheatList.MasterEnabled();
    if (ImGui::Checkbox("Master enable", &master)) {
        m_cheatList.SetMasterEnabled(master);
        PushActiveCodes();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu cheat%s loaded)", m_cheatList.Count(),
                        m_cheatList.Count() == 1 ? "" : "s");
    ImGui::SameLine();
    if (ImGui::SmallButton("Disable all")) {
        m_cheatList.DisableAll();
        PushActiveCodes();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear list")) {
        m_cheatList.Clear();
        PushActiveCodes();
    }

    auto &entries = m_cheatList.Entries();
    size_t removeIndex = static_cast<size_t>(-1);
    bool listChanged = false;

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
            if (ImGui::Checkbox("##en", &entry.enabled)) {
                listChanged = true;
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.name.empty() ? "(unnamed)" : entry.name.c_str());

            ImGui::TableNextColumn();
            for (const auto &c : entry.codes) {
                ImGui::Text("%08X = %0*X  [%s]", c.address, DigitsForCheatWidth(c.width), c.value,
                            CheatWidthName(c.width));
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
        m_cheatList.RemoveAt(removeIndex);
        listChanged = true;
    }
    if (listChanged) {
        PushActiveCodes();
    }
}

void CheatManagerWindow::DrawAddSection() {
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
        } else if (!m_cheatList.AddFromText(name.empty() ? "Unnamed" : name, text, /*enabled=*/false)) {
            m_lastError = "No valid code lines found. Each line should be: ADDR VALUE (hex).";
        } else {
            m_lastError.clear();
            m_newName.fill('\0');
            m_newCode.fill('\0');
            // Newly added entry is disabled, so the active list is unchanged
            // — but push anyway in case master state matters. Cheap call.
            PushActiveCodes();
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
