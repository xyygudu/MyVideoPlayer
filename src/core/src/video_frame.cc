#include "mvp/video_frame.h"

#include "frame_impl.h"

namespace mvp {

VideoFrame::VideoFrame() = default;
VideoFrame::~VideoFrame() = default;

VideoFrame::VideoFrame(VideoFrame&& other) noexcept = default;
VideoFrame& VideoFrame::operator=(VideoFrame&& other) noexcept = default;

const uint8_t* VideoFrame::data(int plane) const {
    if (!impl_ || !impl_->frame || plane < 0 || plane >= 3) return nullptr;
    return impl_->frame->data[plane];
}

int VideoFrame::linesize(int plane) const {
    if (!impl_ || !impl_->frame || plane < 0 || plane >= 3) return 0;
    return impl_->frame->linesize[plane];
}

int VideoFrame::width() const {
    if (!impl_ || !impl_->frame) return 0;
    return impl_->frame->width;
}

int VideoFrame::height() const {
    if (!impl_ || !impl_->frame) return 0;
    return impl_->frame->height;
}

PixelFormat VideoFrame::format() const {
    if (!impl_) return PixelFormat::kUnknown;
    return impl_->format;
}

double VideoFrame::pts() const {
    if (!impl_) return 0.0;
    return impl_->pts;
}

bool VideoFrame::IsValid() const {
    return impl_ && impl_->frame && impl_->frame->data[0];
}

}  // namespace mvp
