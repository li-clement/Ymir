#include <ymir/hw/mpeg/mpeg_card.hpp>

#include <cassert>
#include <utility>

namespace ymir::mpeg {

MPEGCard::MPEGCard() = default;
MPEGCard::~MPEGCard() = default;
MPEGCard::MPEGCard(MPEGCard &&) noexcept = default;
MPEGCard &MPEGCard::operator=(MPEGCard &&) noexcept = default;

void MPEGCard::Reset() {
    m_decoder.Reset();
    m_currentFrame.reset();
    m_status = MPEGCardStatus::Stopped;
    m_interruptFlags = kMPEGCardInterruptNone;
    m_endOfStream = false;
}

void MPEGCard::Initialize() {
    Reset();
}

void MPEGCard::StartPlayback() {
    if (m_status != MPEGCardStatus::Error) {
        m_status = MPEGCardStatus::Playing;
    }
}

void MPEGCard::StopPlayback() {
    if (m_status != MPEGCardStatus::Error) {
        m_status = MPEGCardStatus::Stopped;
    }
}

void MPEGCard::AppendStreamData(std::span<const uint8> data) {
    if (!data.empty()) {
        m_decoder.Append(data);
    }
}

void MPEGCard::SignalEndOfStream() {
    m_endOfStream = true;
    m_decoder.SignalEndOfStream();
}

bool MPEGCard::DecodeNextFrame() {
    if (m_status != MPEGCardStatus::Playing) {
        return false;
    }

    auto frame = m_decoder.DecodeFrame();
    if (!frame.has_value()) {
        if (m_endOfStream) {
            m_status = MPEGCardStatus::Ended;
            m_interruptFlags |= kMPEGCardInterruptStreamEnded;
        }
        return false;
    }

    m_currentFrame = std::move(frame);
    m_interruptFlags |= kMPEGCardInterruptFrameDecoded;
    return true;
}

MPEGCardStatus MPEGCard::GetStatus() const {
    return m_status;
}

bool MPEGCard::HasHeaders() const {
    return m_decoder.HasHeaders();
}

uint32 MPEGCard::GetWidth() const {
    return m_decoder.GetWidth();
}

uint32 MPEGCard::GetHeight() const {
    return m_decoder.GetHeight();
}

double MPEGCard::GetFrameRate() const {
    return m_decoder.GetFrameRate();
}

bool MPEGCard::HasCurrentFrame() const {
    return m_currentFrame.has_value();
}

const DecodedVideoFrame &MPEGCard::GetCurrentFrame() const {
    assert(m_currentFrame.has_value());
    return *m_currentFrame;
}

MPEGCardInterruptFlags MPEGCard::PeekInterruptFlags() const {
    return m_interruptFlags;
}

MPEGCardInterruptFlags MPEGCard::TakeInterruptFlags() {
    const MPEGCardInterruptFlags flags = m_interruptFlags;
    m_interruptFlags = kMPEGCardInterruptNone;
    return flags;
}

void MPEGCard::ClearInterruptFlags(MPEGCardInterruptFlags flags) {
    m_interruptFlags &= ~flags;
}

MPEGCard &MPEGCard::GetGlobal() {
    static MPEGCard g_card;
    return g_card;
}

} // namespace ymir::mpeg
