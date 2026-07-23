#include <ymir/hw/mpeg/mpeg_decoder.hpp>

#define PL_MPEG_IMPLEMENTATION
#include <pl_mpeg.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

namespace ymir::mpeg {

struct MPEGVideoDecoder::Impl {
    // Unified plm_t instance used for MPEG-PS demux+decode (Lunar path).
    plm_buffer_t *buffer = nullptr;
    plm_t *plm = nullptr;

    // Raw video elementary stream decoder (Vatlva path). When non-null,
    // Append/DecodeFrame operate on this instead of the plm_t pipeline.
    // The plm_t above is left untouched so the two paths are fully isolated.
    plm_buffer_t *esBuffer = nullptr;
    plm_video_t *esVideo = nullptr;
    bool esEnded = false;

    int probeCount = 0;

    Impl() {
        Reset();
    }

    ~Impl() {
        Destroy();
    }

    void Destroy() {
        if (esVideo != nullptr) {
            plm_video_destroy(esVideo);
            esVideo = nullptr;
            esBuffer = nullptr; // destroyed by plm_video_destroy (destroy_when_done=TRUE)
        }
        if (plm != nullptr) {
            plm_destroy(plm);
            plm = nullptr;
            buffer = nullptr;
        } else if (buffer != nullptr) {
            plm_buffer_destroy(buffer);
            buffer = nullptr;
        }
    }

    void Reset() {
        Destroy();
        buffer = plm_buffer_create_for_appending(64 * 1024);
        plm = plm_create_with_buffer(buffer, TRUE);
        plm_set_video_enabled(plm, TRUE);
        plm_set_audio_enabled(plm, TRUE);
        probeCount = 0;
        esEnded = false;
    }

    // Initialize the raw video ES decoder. Called once when FeedMPEGStream
    // detects the stream starts with 00 00 01 B3 (MPEG video sequence header)
    // instead of 00 00 01 BA (MPEG-PS pack start code).
    void InitVideoES() {
        if (esVideo != nullptr) {
            return; // already in ES mode
        }
        esBuffer = plm_buffer_create_for_appending(64 * 1024);
        esVideo = plm_video_create_with_buffer(esBuffer, TRUE);
        esEnded = false;
    }

    void ResetVideoES() {
        if (esVideo != nullptr) {
            plm_video_destroy(esVideo);
            esVideo = nullptr;
            esBuffer = nullptr;
        }
        esEnded = false;
    }
};

MPEGVideoDecoder::MPEGVideoDecoder()
    : m_impl(new Impl{}) {}

MPEGVideoDecoder::~MPEGVideoDecoder() {
    delete m_impl;
    m_impl = nullptr;
}

MPEGVideoDecoder::MPEGVideoDecoder(MPEGVideoDecoder &&other) noexcept
    : m_impl(std::exchange(other.m_impl, nullptr)) {}

MPEGVideoDecoder &MPEGVideoDecoder::operator=(MPEGVideoDecoder &&other) noexcept {
    if (this != &other) {
        delete m_impl;
        m_impl = std::exchange(other.m_impl, nullptr);
    }
    return *this;
}

void MPEGVideoDecoder::Reset() {
    assert(m_impl != nullptr);
    m_impl->Reset();
}

void MPEGVideoDecoder::Append(std::span<const uint8> data) {
    assert(m_impl != nullptr);
    if (data.empty()) {
        return;
    }

    if (m_impl->esVideo != nullptr) {
        // Raw video ES mode (Vatlva): feed directly to the video decoder buffer.
        plm_buffer_write(m_impl->esBuffer, const_cast<uint8 *>(data.data()), data.size());
        return;
    }

    // MPEG-PS mode (Lunar): feed to the full plm_t pipeline.
    plm_buffer_write(m_impl->buffer, const_cast<uint8 *>(data.data()), data.size());

    // Probe for audio streams if not yet discovered.
    // Some MPEG files have a system header that declares 0 audio streams, or
    // the system header is 700KB+ into the stream. In both cases,
    // plm_demux_has_headers() sets num_audio_streams=0 and the audio decoder
    // is never created. plm_probe() scans the buffer for actual audio PES
    // packets (0xC0-0xDF), which is more reliable than the system header.
    if (m_impl->probeCount < 2000 && plm_get_num_audio_streams(m_impl->plm) == 0) {
        plm_probe(m_impl->plm, 1024 * 1024);
        m_impl->probeCount++;
    }
}

void MPEGVideoDecoder::InitVideoES() {
    assert(m_impl != nullptr);
    m_impl->InitVideoES();
}

bool MPEGVideoDecoder::IsVideoES() const {
    assert(m_impl != nullptr);
    return m_impl->esVideo != nullptr;
}

void MPEGVideoDecoder::ResetVideoES() {
    assert(m_impl != nullptr);
    m_impl->ResetVideoES();
}

void MPEGVideoDecoder::SignalEndOfStream() {
    assert(m_impl != nullptr);
    if (m_impl->esVideo != nullptr) {
        plm_buffer_signal_end(m_impl->esBuffer);
        return;
    }
    plm_buffer_signal_end(m_impl->buffer);
}

bool MPEGVideoDecoder::HasHeaders() const {
    assert(m_impl != nullptr);
    if (m_impl->esVideo != nullptr) {
        return plm_video_has_header(m_impl->esVideo) != FALSE;
    }
    return plm_has_headers(m_impl->plm) != FALSE;
}

uint32 MPEGVideoDecoder::GetWidth() const {
    assert(m_impl != nullptr);
    if (m_impl->esVideo != nullptr) {
        return std::max(0, plm_video_get_width(m_impl->esVideo));
    }
    return std::max(0, plm_get_width(m_impl->plm));
}

uint32 MPEGVideoDecoder::GetHeight() const {
    assert(m_impl != nullptr);
    if (m_impl->esVideo != nullptr) {
        return std::max(0, plm_video_get_height(m_impl->esVideo));
    }
    return std::max(0, plm_get_height(m_impl->plm));
}

double MPEGVideoDecoder::GetFrameRate() const {
    assert(m_impl != nullptr);
    if (m_impl->esVideo != nullptr) {
        return plm_video_get_framerate(m_impl->esVideo);
    }
    return plm_get_framerate(m_impl->plm);
}

bool MPEGVideoDecoder::HasAudio() const {
    assert(m_impl != nullptr);
    // Raw video ES has no audio.
    if (m_impl->esVideo != nullptr) {
        return false;
    }
    return plm_get_num_audio_streams(m_impl->plm) > 0;
}

uint32 MPEGVideoDecoder::GetAudioSampleRate() const {
    assert(m_impl != nullptr);
    if (m_impl->esVideo != nullptr) {
        return 0;
    }
    if (plm_get_num_audio_streams(m_impl->plm) == 0) {
        return 0;
    }
    // plm doesn't expose the audio decoder directly, but the high-level API
    // always uses 44100Hz for MPEG-1 Layer II.
    return 44100;
}

std::optional<DecodedVideoFrame> MPEGVideoDecoder::DecodeFrame() {
    assert(m_impl != nullptr);

    plm_frame_t *plmFrame = nullptr;
    if (m_impl->esVideo != nullptr) {
        // Raw video ES decode: one picture per call.
        plmFrame = plm_video_decode(m_impl->esVideo);
    } else {
        plmFrame = plm_decode_video(m_impl->plm);
    }
    if (plmFrame == nullptr) {
        return std::nullopt;
    }

    DecodedVideoFrame frame{};
    frame.time = plmFrame->time;
    frame.width = plmFrame->width;
    frame.height = plmFrame->height;
    frame.pixelsXBGR8888.assign(static_cast<size_t>(frame.width) * frame.height, 0xFF000000u);

    plm_frame_to_rgba(plmFrame, reinterpret_cast<uint8 *>(frame.pixelsXBGR8888.data()), frame.width * sizeof(uint32));

    return frame;
}

std::optional<DecodedAudioFrame> MPEGVideoDecoder::DecodeAudio() {
    assert(m_impl != nullptr);

    // Raw video ES has no audio.
    if (m_impl->esVideo != nullptr) {
        return std::nullopt;
    }

    plm_samples_t *plmSamples = plm_decode_audio(m_impl->plm);
    if (plmSamples == nullptr) {
        return std::nullopt;
    }

    DecodedAudioFrame audio{};
    audio.time = plmSamples->time;

    // Convert float [-1.0, 1.0] to sint16
    for (uint32 i = 0; i < DecodedAudioFrame::kSamplesPerFrame * 2; ++i) {
        float sample = plmSamples->interleaved[i];
        // Clamp to [-1.0, 1.0] and scale to sint16 range
        if (sample > 1.0f) {
            sample = 1.0f;
        } else if (sample < -1.0f) {
            sample = -1.0f;
        }
        audio.samples[i] = static_cast<sint16>(sample * 32767.0f);
    }

    return audio;
}

bool MPEGVideoDecoder::HasEnded() const {
    assert(m_impl != nullptr);
    if (m_impl->esVideo != nullptr) {
        return plm_video_has_ended(m_impl->esVideo) != FALSE;
    }
    // plm_has_ended covers demux, video, and audio sub-states. After
    // SignalEndOfStream(), the buffer signals end to demux which eventually
    // propagates the has_ended flag here once all buffered frames have been
    // consumed.
    return plm_has_ended(m_impl->plm) != FALSE;
}

} // namespace ymir::mpeg
