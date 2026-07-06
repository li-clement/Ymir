#pragma once

#include <ymir/core/types.hpp>

#include <optional>
#include <span>
#include <vector>

namespace ymir::mpeg {

struct DecodedVideoFrame {
    double time = 0.0;
    uint32 width = 0;
    uint32 height = 0;

    // Pixels are stored in the same little-endian XBGR8888 layout used by the
    // software VDP renderer's framebuffer: byte order R, G, B, opaque padding.
    std::vector<uint32> pixelsXBGR8888;
};

// Decoded MPEG-1 audio frame (MP2).
// Contains PLM_AUDIO_SAMPLES_PER_FRAME (1152) stereo samples as interleaved
// sint16 L,R,L,R,... normalized from plm's float output.
struct DecodedAudioFrame {
    double time = 0.0;
    static constexpr uint32 kSamplesPerFrame = 1152;
    std::array<sint16, kSamplesPerFrame * 2> samples; // interleaved L/R
};

class MPEGVideoDecoder {
public:
    MPEGVideoDecoder();
    ~MPEGVideoDecoder();

    MPEGVideoDecoder(const MPEGVideoDecoder &) = delete;
    MPEGVideoDecoder &operator=(const MPEGVideoDecoder &) = delete;

    MPEGVideoDecoder(MPEGVideoDecoder &&) noexcept;
    MPEGVideoDecoder &operator=(MPEGVideoDecoder &&) noexcept;

    void Reset();

    void Append(std::span<const uint8> data);
    void SignalEndOfStream();

    [[nodiscard]] bool HasHeaders() const;
    [[nodiscard]] uint32 GetWidth() const;
    [[nodiscard]] uint32 GetHeight() const;
    [[nodiscard]] double GetFrameRate() const;

    [[nodiscard]] bool HasAudio() const;
    [[nodiscard]] uint32 GetAudioSampleRate() const;

    [[nodiscard]] std::optional<DecodedVideoFrame> DecodeFrame();
    [[nodiscard]] std::optional<DecodedAudioFrame> DecodeAudio();

private:
    struct Impl;
    Impl *m_impl = nullptr;
};

} // namespace ymir::mpeg
