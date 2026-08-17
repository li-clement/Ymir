#include <ymir/sys/cheats.hpp>

namespace ymir::sys {

void CheatEngine::SetActiveCodes(std::vector<CheatCode> codes) {
    std::lock_guard lock{m_mutex};
    m_codes = std::move(codes);
}

void CheatEngine::Clear() {
    std::lock_guard lock{m_mutex};
    m_codes.clear();
}

size_t CheatEngine::Count() const {
    std::lock_guard lock{m_mutex};
    return m_codes.size();
}

void CheatEngine::ApplyAll(SH2Bus &bus) const {
    std::lock_guard lock{m_mutex};
    for (const auto &c : m_codes) {
        switch (c.width) {
        case CheatWidth::Byte: //
            bus.Poke<uint8>(c.address, static_cast<uint8>(c.value));
            break;
        case CheatWidth::Word: //
            bus.Poke<uint16>(c.address, static_cast<uint16>(c.value));
            break;
        case CheatWidth::Long: //
            bus.Poke<uint32>(c.address, c.value);
            break;
        }
    }
}

} // namespace ymir::sys
