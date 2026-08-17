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
    // NOTE: do NOT clear m_currentFrame here. The VDP2 overlay may need to
    // keep showing the last decoded frame after a user-initiated FMV exit
    // (Lunar Start-during-FMV) so the screen doesn't go black before the
    // title screen takes over. MpegInit() / MpegPlay() will overwrite the
    // frame when the next FMV starts.
    // m_currentFrame.reset();
    m_currentAudio.reset();
    m_audioSampleIndex = 0;
    m_status = MPEGCardStatus::Stopped;
    m_interruptFlags = kMPEGCardInterruptNone;
    m_endOfStream = false;
    m_displayEnabled = false;
    m_hostDecodeSteps = 0;
    m_decodeTiming = MPEGDecodeTiming::VSYNC;
    m_videoPaused = false;
    m_videoFrozen = false;
    m_playMode = 0;
}

void MPEGCard::Initialize() {
    Reset();
}

void MPEGCard::OpenNewPlaybackPipeline() {
    // Match erings: $95 after a spent stream replaces the play object.
    // Without this, pl_mpeg still holds the previous PS/ES + EOF flag and the
    // VDP pacing clock still compares against the old frame.time - Lunar's
    // 2nd/3rd in-game FMVs then flash wrong-stream frames or skip ahead.
    m_decoder.Reset();
    m_currentFrame.reset();
    m_currentAudio.reset();
    m_audioSampleIndex = 0;
    m_endOfStream = false;
    m_interruptFlags = kMPEGCardInterruptNone;
    m_presentationClockReset = true;
    m_hostDecodeSteps = 0;
}

void MPEGCard::StartPlayback() {
    if (m_status == MPEGCardStatus::Error) {
        return;
    }
    // Fresh pipeline when not already mid-play. Re-Play while Playing keeps
    // the current decoder (host re-issue); Stopped/Ended always reopen.
    if (m_status != MPEGCardStatus::Playing) {
        OpenNewPlaybackPipeline();
    }
    m_status = MPEGCardStatus::Playing;
    m_audioSampleIndex = 0;
}

void MPEGCard::StopPlayback() {
    // Once the stream has reached Ended, keep it Ended — never regress
    // to Stopped, because the game's $AF poll depends on this state and
    // would otherwise re-enter its MPEG wait loop. Also promote
    // Playing -> Ended so a stop-while-playing request (e.g. user pressed
    // Start during FMV) is observable to the game as "stream finished".
    if (m_status == MPEGCardStatus::Playing) {
        m_status = MPEGCardStatus::Ended;
        m_interruptFlags |= kMPEGCardInterruptStreamEnded;
    }
    // Drop any residual MPEG audio immediately. Video keeps the last frame
    // for the overlay handoff; audio must not keep mixing into SCSP after
    // the game has disconnected / skipped (Lunar title BGM otherwise fights
    // FMV audio for several seconds while pl_mpeg drains its ES buffer).
    m_currentAudio.reset();
    m_audioSampleIndex = 0;
}

bool MPEGCard::ConsumePresentationClockReset() {
    const bool reset = m_presentationClockReset;
    m_presentationClockReset = false;
    return reset;
}

void MPEGCard::AppendStreamData(std::span<const uint8> data) {
    if (data.empty()) {
        return;
    }
    // Soft reopen: sectors arrived after end without a preceding $95.
    // Never append into a spent pl_mpeg (EOF already signalled) — that
    // concatenates streams and is the multi-FMV "wrong clip" failure mode.
    if (m_status == MPEGCardStatus::Ended || m_endOfStream) {
        OpenNewPlaybackPipeline();
        m_status = MPEGCardStatus::Playing;
    }
    m_decoder.Append(data);
}

void MPEGCard::RequestEndOfStream() {
    m_endOfStream = true;
    m_decoder.SignalEndOfStream();
}

void MPEGCard::SignalEndOfStream() {
    RequestEndOfStream();
    // Transition to Ended so MpegGetStatus can report videoEnded=true.
    // The game uses this to exit the MPEG polling loop.
    if (m_status != MPEGCardStatus::Error) {
        m_status = MPEGCardStatus::Ended;
        m_interruptFlags |= kMPEGCardInterruptStreamEnded;
    }
    // NOTE: do NOT clear m_currentFrame here. The VDP2 overlay needs to keep
    // showing the final video frame after stream end (e.g. Lunar uses the
    // last decoded frame as a backdrop until the title screen takes over).
    // The overlay rendering path checks `HasCurrentFrame()` and displays
    // whatever was decoded last.
    //
    // Audio is the opposite: once the stream/card is ended, stop mixing
    // residual samples so title BGM is not covered by leftover FMV audio.
    m_currentAudio.reset();
    m_audioSampleIndex = 0;
}

bool MPEGCard::DecodeNextFrame() {
    // $96 pause-time 0 halts picture decode (erings vidPaused).
    if (m_videoPaused) {
        return false;
    }
    auto frame = m_decoder.DecodeFrame();
    if (!frame.has_value()) {
        // No more frames available. Once pl_mpeg has finalised end-of-stream
        // (or we were told via SignalEndOfStream and all buffered frames were
        // consumed), promote the card status from Playing to Ended so the
        // game's $AF poll exits its MPEG playback loop. Stay Ended across
        // continued decode attempts until the next CmdMpegPlay resets state.
        if (m_status == MPEGCardStatus::Playing && (m_endOfStream || m_decoder.HasEnded())) {
            m_status = MPEGCardStatus::Ended;
            m_interruptFlags |= kMPEGCardInterruptStreamEnded;
            m_currentAudio.reset();
            m_audioSampleIndex = 0;
        }
        return false;
    }

    // $96 freeze-time 0 holds the displayed picture while decode continues
    // (erings vidFrozen). The frame is decoded (advancing internal reference
    // frames) but not published to the overlay.
    if (!m_videoFrozen) {
        m_currentFrame = std::move(frame);
    }
    m_interruptFlags |= kMPEGCardInterruptFrameDecoded;
    return true;
}

bool MPEGCard::DecodeNextAudioFrame() {
    // Only pull audio while actively playing. After StopPlayback /
    // SignalEndOfStream the card is Ended and residual ES must not be
    // decoded into the SCSP mix (would bleed FMV audio over title BGM).
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

bool MPEGCard::HasStreamEnded() const {
    // RequestEndOfStream() only marks input complete. The stream ends for
    // playback once pl_mpeg has consumed all buffered frames.
    return m_decoder.HasEnded();
}

bool MPEGCard::IsEndOfStreamRequested() const {
    return m_endOfStream;
}

const DecodedAudioFrame &MPEGCard::GetCurrentAudio() const {
    assert(m_currentAudio.has_value());
    return *m_currentAudio;
}

bool MPEGCard::GetNextAudioSample(sint16 &left, sint16 &right) {
    // Mix only while Playing. StopPlayback / SignalEndOfStream clear the
    // current audio buffer and flip status to Ended, so the SCSP callback
    // must not keep draining pl_mpeg after the game has skipped FMV.
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
