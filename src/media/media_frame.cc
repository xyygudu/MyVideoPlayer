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

int MediaFrame::width() const { return frame_.get() ? frame_->width : 0; }
int MediaFrame::height() const { return frame_.get() ? frame_->height : 0; }
int MediaFrame::format() const { return frame_.get() ? frame_->format : -1; }

const uint8_t* MediaFrame::PlaneData(int plane) const {
    return frame_.get() ? frame_->data[plane] : nullptr;
}
uint8_t* MediaFrame::PlaneData(int plane) {
    return frame_.get() ? frame_->data[plane] : nullptr;
}
int MediaFrame::PlaneLinesize(int plane) const {
    return frame_.get() ? frame_->linesize[plane] : 0;
}

MediaFrame MediaFrame::MakeWritable() const {
    if (!frame_.get()) return MediaFrame();
    AVFramePtr tmp;
    av_frame_ref(tmp.get(), frame_.get());
    av_frame_make_writable(tmp.get());
    MediaFrame copy;
    copy.frame_ = std::move(tmp);
    copy.pts_ = pts_;
    copy.type_ = type_;
    return copy;
}

MediaFrame MediaFrame::CreateSameFormat(const MediaFrame& ref, double pts) {
    MediaFrame mf;
    if (!ref.frame_.get()) return mf;
    mf.frame_->format = ref.frame_->format;
    mf.frame_->width = ref.frame_->width;
    mf.frame_->height = ref.frame_->height;
    av_frame_get_buffer(mf.frame_.get(), 0);
    mf.pts_ = pts;
    mf.type_ = ref.type_;
    return mf;
}

}  // namespace mvp
