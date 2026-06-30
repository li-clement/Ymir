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

    [[nodiscard]] MPEGCardStatus GetStatus() const;

    [[nodiscard]] bool HasHeaders() const;
    [[nodiscard]] uint32 GetWidth() const;
    [[nodiscard]] uint32 GetHeight() const;
    [[nodiscard]] double GetFrameRate() const;

    [[nodiscard]] bool HasCurrentFrame() const;
    [[nodiscard]] const DecodedVideoFrame &GetCurrentFrame() const;

    [[nodiscard]] MPEGCardInterruptFlags PeekInterruptFlags() const;
    [[nodiscard]] MPEGCardInterruptFlags TakeInterruptFlags();
    void ClearInterruptFlags(MPEGCardInterruptFlags flags);

private:
    MPEGVideoDecoder m_decoder;
    std::optional<DecodedVideoFrame> m_currentFrame;
    MPEGCardStatus m_status = MPEGCardStatus::Stopped;
    MPEGCardInterruptFlags m_interruptFlags = kMPEGCardInterruptNone;
    bool m_endOfStream = false;
};

} // namespace ymir::mpeg
