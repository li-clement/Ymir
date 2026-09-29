#pragma once

#include "mpeg_decoder.hpp"

#include <ymir/core/types.hpp>

#include <optional>
#include <span>

namespace ymir::mpeg {

enum class MPEGCardStatus : uint8 {
    Stopped,
    Playing,
    Ended,
    Error,
};

// Decode timing mode ($94 SetMode CR2 high byte).
enum class MPEGDecodeTiming : uint8 {
    VSYNC = 0, // VSYNC-synchronized: decode paced by the pacing loop clock
    Host = 1,  // host-synchronized: one picture per $97 MpegOutDecodingSync
};

using MPEGCardInterruptFlags = uint16;
inline constexpr MPEGCardInterruptFlags kMPEGCardInterruptNone = 0x0000;
// MPEG interrupt cause bits (erings mpegInt*). The $91 response packs these
// as a 24-bit cause register: bits 23-16 in CR1 low byte, bits 15-0 in CR2.
// kMPEGCardInterruptFrameDecoded maps to cause bit $000100 (picture-start),
// which Vatlva's $92 mask 0x000500 (picture-start | sequence-end) gates.
// We store the low 16 bits in the flags word; $91 returns them in CR2 and
// the high byte (currently always 0) in CR1 low byte.
inline constexpr MPEGCardInterruptFlags kMPEGCardInterruptPictureStart = 0x0100;  // cause $000100
inline constexpr MPEGCardInterruptFlags kMPEGCardInterruptSequenceStart = 0x0800; // cause $000800
inline constexpr MPEGCardInterruptFlags kMPEGCardInterruptStreamEnded = 0x0400;  // cause $000400 (sequence end)
inline constexpr MPEGCardInterruptFlags kMPEGCardInterruptFrameDecoded = kMPEGCardInterruptPictureStart;
inline constexpr MPEGCardInterruptFlags kMPEGCardInterruptError = 0x8000;

class MPEGCard {
public:
    MPEGCard();
    ~MPEGCard();

    MPEGCard(const MPEGCard &) = delete;
    MPEGCard &operator=(const MPEGCard &) = delete;

    MPEGCard(MPEGCard &&) noexcept;
    MPEGCard &operator=(MPEGCard &&) noexcept;

    void Reset();
    void Initialize();

    void StartPlayback();
    void StopPlayback();

    // $A0 MpegDisplay display switch (CR2 high byte: 0=off, nonzero=on).
    void SetDisplayEnabled(bool enabled) { m_displayEnabled = enabled; }
    [[nodiscard]] bool IsDisplayEnabled() const { return m_displayEnabled; }

    // True once after a new play pipeline is opened (StartPlayback / reopen).
    [[nodiscard]] bool ConsumePresentationClockReset();

    void AppendStreamData(std::span<const uint8> data);
    // Mark input EOF while allowing buffered frames to drain.
    void RequestEndOfStream();
    void SignalEndOfStream();

    [[nodiscard]] bool DecodeNextFrame();
    [[nodiscard]] bool DecodeNextAudioFrame();

    // $97 MpegOutDecodingSync: arm one host-synchronized decode step.
    // Used by Vatlva which steps pictures itself instead of relying on the
    // VSYNC pacing clock.
    void RequestHostDecodeStep() { m_hostDecodeSteps = std::min(m_hostDecodeSteps + 1, 4); }

    // Returns true if a host-synchronized decode step is pending ($97).
    [[nodiscard]] bool HasHostDecodeStep() const { return m_hostDecodeSteps > 0; }

    // Consume one host decode step (called when the pacing loop decodes a
    // frame in response to $97). Returns true if a step was consumed.
    bool ConsumeHostDecodeStep() {
        if (m_hostDecodeSteps > 0) {
            m_hostDecodeSteps--;
            return true;
        }
        return false;
    }

    // Decode timing mode ($94). VSYNC = pacing-loop-driven, Host = $97-driven.
    void SetDecodeTiming(MPEGDecodeTiming timing) { m_decodeTiming = timing; }
    [[nodiscard]] MPEGDecodeTiming GetDecodeTiming() const { return m_decodeTiming; }

    // $96 SetDecodeMethod: video pause/freeze.
    void SetVideoPaused(bool paused) { m_videoPaused = paused; }
    void SetVideoFrozen(bool frozen) { m_videoFrozen = frozen; }
    [[nodiscard]] bool IsVideoPaused() const { return m_videoPaused; }

    // Switch the decoder into raw video ES mode (Vatlva). The card will
    // feed sectors to the ES buffer instead of the PS demuxer.
    void InitVideoES() { m_decoder.InitVideoES(); }
    [[nodiscard]] bool IsVideoES() const { return m_decoder.IsVideoES(); }

    [[nodiscard]] MPEGCardStatus GetStatus() const;

    [[nodiscard]] bool HasHeaders() const;
    [[nodiscard]] uint32 GetWidth() const;
    [[nodiscard]] uint32 GetHeight() const;
    [[nodiscard]] double GetFrameRate() const;

    [[nodiscard]] bool HasAudio() const;
    [[nodiscard]] uint32 GetAudioSampleRate() const;

    [[nodiscard]] bool HasCurrentFrame() const;
    [[nodiscard]] const DecodedVideoFrame &GetCurrentFrame() const;

    // Test-only helper: inject a synthetic frame so overlay tests can verify
    // window placement without having to drive pl_mpeg through a full decode
    // cycle. Not part of the production surface (do not use outside tests).
    void SetCurrentFrameForTest(DecodedVideoFrame frame) { m_currentFrame = std::move(frame); }

    [[nodiscard]] bool HasCurrentAudio() const;
    [[nodiscard]] const DecodedAudioFrame &GetCurrentAudio() const;

    [[nodiscard]] bool HasStreamEnded() const;
    [[nodiscard]] bool IsEndOfStreamRequested() const;

    bool GetNextAudioSample(sint16 &left, sint16 &right);

    [[nodiscard]] MPEGCardInterruptFlags PeekInterruptFlags() const;
    [[nodiscard]] MPEGCardInterruptFlags TakeInterruptFlags();
    void ClearInterruptFlags(MPEGCardInterruptFlags flags);

    // $95 MpegPlay play mode: 0=A/V synchronized, 1=independent playback.
    // Independent playback means no A/V start hold; video decodes immediately.
    void SetPlayMode(uint8 mode) { m_playMode = mode; }
    [[nodiscard]] uint8 GetPlayMode() const { return m_playMode; }

private:
    MPEGVideoDecoder m_decoder;
    std::optional<DecodedVideoFrame> m_currentFrame;
    std::optional<DecodedAudioFrame> m_currentAudio;
    uint32 m_audioSampleIndex = 0;
    MPEGCardStatus m_status = MPEGCardStatus::Stopped;
    MPEGCardInterruptFlags m_interruptFlags = kMPEGCardInterruptNone;
    bool m_endOfStream = false;
    bool m_displayEnabled = false;
    bool m_presentationClockReset = false;

    // Vatlva: host-synchronized decode ($97 steps).
    int m_hostDecodeSteps = 0;
    MPEGDecodeTiming m_decodeTiming = MPEGDecodeTiming::VSYNC;
    bool m_videoPaused = false;
    bool m_videoFrozen = false;
    uint8 m_playMode = 0;

    void OpenNewPlaybackPipeline();
};

} // namespace ymir::mpeg
