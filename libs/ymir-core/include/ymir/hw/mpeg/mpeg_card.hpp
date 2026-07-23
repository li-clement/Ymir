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

using MPEGCardInterruptFlags = uint16;
inline constexpr MPEGCardInterruptFlags kMPEGCardInterruptNone = 0x0000;
inline constexpr MPEGCardInterruptFlags kMPEGCardInterruptFrameDecoded = 0x0001;
inline constexpr MPEGCardInterruptFlags kMPEGCardInterruptStreamEnded = 0x0002;
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
    // When off, the video overlay must not blit -- the game turns the MPEG
    // display off to let VDP2 show the title screen (or any non-FMV screen).
    void SetDisplayEnabled(bool enabled) { m_displayEnabled = enabled; }
    [[nodiscard]] bool IsDisplayEnabled() const { return m_displayEnabled; }

    // True once after a new play pipeline is opened (StartPlayback / reopen).
    // Saturn's frame-pacing clock must restart at 0 so the previous FMV's
    // frame.time cannot force the next stream to skip or show wrong frames.
    [[nodiscard]] bool ConsumePresentationClockReset();

    void AppendStreamData(std::span<const uint8> data);
    // Mark input EOF while allowing buffered frames to drain.
    void RequestEndOfStream();
    void SignalEndOfStream();

    [[nodiscard]] bool DecodeNextFrame();
    [[nodiscard]] bool DecodeNextAudioFrame();

    [[nodiscard]] MPEGCardStatus GetStatus() const;

    [[nodiscard]] bool HasHeaders() const;
    [[nodiscard]] uint32 GetWidth() const;
    [[nodiscard]] uint32 GetHeight() const;
    [[nodiscard]] double GetFrameRate() const;

    [[nodiscard]] bool HasAudio() const;
    [[nodiscard]] uint32 GetAudioSampleRate() const;

    [[nodiscard]] bool HasCurrentFrame() const;
    [[nodiscard]] const DecodedVideoFrame &GetCurrentFrame() const;

    [[nodiscard]] bool HasCurrentAudio() const;
    [[nodiscard]] const DecodedAudioFrame &GetCurrentAudio() const;

    // True when the underlying pl_mpeg decoder has signalled end-of-stream
    // and consumed all buffered frames. Used by Saturn's pacing loop to
    // transition MPEGCardStatus::Playing -> Ended so the game can poll \$AF
    // and exit its MPEG playback loop.
    [[nodiscard]] bool HasStreamEnded() const;
    [[nodiscard]] bool IsEndOfStreamRequested() const;

    // Retrieves the next stereo audio sample from the decoded audio stream.
    // Automatically decodes the next audio frame when the current one is exhausted.
    // Returns false unless status is Playing (no residual mix after skip/EOF),
    // or if no audio stream exists / decode fails.
    bool GetNextAudioSample(sint16 &left, sint16 &right);

    [[nodiscard]] MPEGCardInterruptFlags PeekInterruptFlags() const;
    [[nodiscard]] MPEGCardInterruptFlags TakeInterruptFlags();
    void ClearInterruptFlags(MPEGCardInterruptFlags flags);

private:
    MPEGVideoDecoder m_decoder;
    std::optional<DecodedVideoFrame> m_currentFrame;
    std::optional<DecodedAudioFrame> m_currentAudio;
    uint32 m_audioSampleIndex = 0; // position within m_currentAudio
    MPEGCardStatus m_status = MPEGCardStatus::Stopped;
    MPEGCardInterruptFlags m_interruptFlags = kMPEGCardInterruptNone;
    bool m_endOfStream = false;
    bool m_displayEnabled = false; // $A0 MpegDisplay switch
    bool m_presentationClockReset = false;

    // Drop residual decoder ES/PS state and presentation buffers so the next
    // FMV cannot show frames from the previous stream. Display enable is left
    // alone (controlled by $A0). Used by StartPlayback and soft reopen.
    void OpenNewPlaybackPipeline();
};

} // namespace ymir::mpeg
