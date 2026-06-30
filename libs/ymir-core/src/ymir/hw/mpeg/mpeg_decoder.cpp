#include <ymir/hw/mpeg/mpeg_decoder.hpp>

#define PL_MPEG_IMPLEMENTATION
#include <pl_mpeg.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

namespace ymir::mpeg {

struct MPEGVideoDecoder::Impl {
    plm_buffer_t *buffer = nullptr;
    plm_t *plm = nullptr;

    Impl() {
        Reset();
    }

    ~Impl() {
        Destroy();
    }

    void Destroy() {
        if (plm != nullptr) {
            // plm owns and destroys the buffer because create_with_buffer(..., TRUE)
            // is used in Reset().
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
        plm_set_audio_enabled(plm, FALSE);
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
    plm_buffer_write(m_impl->buffer, const_cast<uint8 *>(data.data()), data.size());
}

void MPEGVideoDecoder::SignalEndOfStream() {
    assert(m_impl != nullptr);
    plm_buffer_signal_end(m_impl->buffer);
}

bool MPEGVideoDecoder::HasHeaders() const {
    assert(m_impl != nullptr);
    return plm_has_headers(m_impl->plm) != FALSE;
}

uint32 MPEGVideoDecoder::GetWidth() const {
    assert(m_impl != nullptr);
    return std::max(0, plm_get_width(m_impl->plm));
}

uint32 MPEGVideoDecoder::GetHeight() const {
    assert(m_impl != nullptr);
    return std::max(0, plm_get_height(m_impl->plm));
}

double MPEGVideoDecoder::GetFrameRate() const {
    assert(m_impl != nullptr);
    return plm_get_framerate(m_impl->plm);
}

std::optional<DecodedVideoFrame> MPEGVideoDecoder::DecodeFrame() {
    assert(m_impl != nullptr);

    plm_frame_t *plmFrame = plm_decode_video(m_impl->plm);
    if (plmFrame == nullptr) {
        return std::nullopt;
    }

    DecodedVideoFrame frame{};
    frame.time = plmFrame->time;
    frame.width = plmFrame->width;
    frame.height = plmFrame->height;
    frame.pixelsXBGR8888.assign(static_cast<size_t>(frame.width) * frame.height, 0xFF000000u);

    // plm writes RGBA byte order. On little-endian hosts this is exactly the
    // uint32 layout used by Ymir's software framebuffer: 0xAABBGGRR.
    plm_frame_to_rgba(plmFrame, reinterpret_cast<uint8 *>(frame.pixelsXBGR8888.data()), frame.width * sizeof(uint32));
    return frame;
}

} // namespace ymir::mpeg
