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

    void AppendStreamData(std::span<const uint8> data);
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

    // Retrieves the next stereo audio sample from the decoded audio stream.
    // Automatically decodes the next audio frame when the current one is exhausted.
    // Returns false if playback is stopped, no audio stream exists, or the stream has ended.
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
};

} // namespace ymir::mpeg
