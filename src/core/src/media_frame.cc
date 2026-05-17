#include "media_frame.h"

#include <utility>

extern "C" {
#include <libavutil/frame.h>
}

namespace mvp {

MediaFrame::MediaFrame() = default;

MediaFrame::MediaFrame(AVFrame* src, double pts, MediaType type)
    : pts_(pts), type_(type) {
    if (src) {
        av_frame_ref(frame_.get(), src);
    }
}

MediaFrame::~MediaFrame() = default;

MediaFrame::MediaFrame(MediaFrame&& other) noexcept
    : frame_(std::move(other.frame_)),
      pts_(other.pts_),
      type_(other.type_) {
    other.pts_ = 0.0;
    other.type_ = MediaType::kUnknown;
}

MediaFrame& MediaFrame::operator=(MediaFrame&& other) noexcept {
    if (this != &other) {
        frame_ = std::move(other.frame_);
        pts_ = other.pts_;
        type_ = other.type_;
        other.pts_ = 0.0;
        other.type_ = MediaType::kUnknown;
    }
    return *this;
}

double MediaFrame::pts() const { return pts_; }

bool MediaFrame::IsValid() const {
    return frame_.get() && frame_.get()->data[0];
}

MediaType MediaFrame::type() const { return type_; }

AVFrame* MediaFrame::RawFrame() const { return frame_.get(); }

}  // namespace mvp
