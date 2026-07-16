#include <ymir/hw/mpeg/mpeg_card.hpp>

#include <fmt/format.h>

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
    m_currentAudio.reset();
    m_audioSampleIndex = 0;
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
        m_audioSampleIndex = 0;
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
        // Do not auto-transition to Playing state.
        // The game checks MpegGetStatus and expects Stopped (0000) even
        // during video playback, matching Kronos behavior.
    }
}

void MPEGCard::SignalEndOfStream() {
    m_endOfStream = true;
    m_decoder.SignalEndOfStream();
    // Transition to Ended immediately rather than waiting for pl_mpeg to
    // exhaust its internal buffer, which can take many frames and causes
    // the game to time out while polling MpegGetStatus.
    if (m_status == MPEGCardStatus::Playing) {
        m_status = MPEGCardStatus::Ended;
        m_interruptFlags |= kMPEGCardInterruptStreamEnded;
    }
}

bool MPEGCard::DecodeNextFrame() {
    // Decode frames regardless of card status (Stopped/Playing/Ended).
    // The card status is kept at Stopped for game compatibility (matching
    // Kronos), but we still need to decode video frames for the overlay.
    auto frame = m_decoder.DecodeFrame();
    if (!frame.has_value()) {
        static int no_frame_count = 0;
        if (no_frame_count++ % 60 == 0) {
            fmt::print(stderr, "MPEGCard: DecodeFrame returned no frame. EOS={}\n", m_endOfStream);
        }
        if (m_endOfStream) {
            m_status = MPEGCardStatus::Ended;
            m_interruptFlags |= kMPEGCardInterruptStreamEnded;
        }
        return false;
    }
    fmt::print(stderr, "MPEGCard: DecodeFrame successful! {}x{}\n", frame->width, frame->height);

    m_currentFrame = std::move(frame);
    m_interruptFlags |= kMPEGCardInterruptFrameDecoded;
    return true;
}

bool MPEGCard::DecodeNextAudioFrame() {
    if (m_status != MPEGCardStatus::Playing) {
        return false;
    }

    auto audio = m_decoder.DecodeAudio();
    if (!audio.has_value()) {
        return false;
    }

    m_currentAudio = std::move(audio);
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

bool MPEGCard::HasAudio() const {
    return m_decoder.HasAudio();
}

uint32 MPEGCard::GetAudioSampleRate() const {
    return m_decoder.GetAudioSampleRate();
}

bool MPEGCard::HasCurrentFrame() const {
    return m_currentFrame.has_value();
}

const DecodedVideoFrame &MPEGCard::GetCurrentFrame() const {
    assert(m_currentFrame.has_value());
    return *m_currentFrame;
}

bool MPEGCard::HasCurrentAudio() const {
    return m_currentAudio.has_value();
}

const DecodedAudioFrame &MPEGCard::GetCurrentAudio() const {
    assert(m_currentAudio.has_value());
    return *m_currentAudio;
}

bool MPEGCard::GetNextAudioSample(sint16 &left, sint16 &right) {
    if (m_status != MPEGCardStatus::Playing || !m_decoder.HasAudio()) {
        return false;
    }

    // Decode next audio frame if current one is exhausted or absent
    if (!m_currentAudio.has_value() || m_audioSampleIndex >= DecodedAudioFrame::kSamplesPerFrame) {
        if (!DecodeNextAudioFrame()) {
            return false;
        }
        m_audioSampleIndex = 0;
    }

    const uint32 idx = m_audioSampleIndex * 2;
    left = m_currentAudio->samples[idx + 0];
    right = m_currentAudio->samples[idx + 1];
    ++m_audioSampleIndex;
    return true;
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

} // namespace ymir::mpeg
