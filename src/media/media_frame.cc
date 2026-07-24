#include "media_frame.h"

#include <utility>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
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

MediaFrame MediaFrame::MakeWritable() && {
    if (!frame_.get()) return MediaFrame();
    if (av_frame_is_writable(frame_.get())) {
        // Already the sole owner of the underlying buffer(s) — hand off
        // ownership directly, no extra ref, no copy.
        return std::move(*this);
    }
    AVFramePtr tmp;
    av_frame_ref(tmp.get(), frame_.get());
    av_frame_make_writable(tmp.get());
    MediaFrame copy;
    copy.frame_ = std::move(tmp);
    copy.pts_ = pts_;
    copy.type_ = type_;
    return copy;
}

MediaFrame MediaFrame::MakeWritable() const& {
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

// --- MediaFramePool ---

namespace {
constexpr int kFramePoolAlign = 32;  // AVX2-friendly alignment for future SIMD reads
}  // namespace

MediaFramePool::~MediaFramePool() { av_buffer_pool_uninit(&pool_); }

MediaFramePool::MediaFramePool(MediaFramePool&& other) noexcept
    : pool_(other.pool_),
      width_(other.width_),
      height_(other.height_),
      format_(other.format_) {
    other.pool_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
    other.format_ = -1;
}

MediaFramePool& MediaFramePool::operator=(MediaFramePool&& other) noexcept {
    if (this != &other) {
        av_buffer_pool_uninit(&pool_);
        pool_ = other.pool_;
        width_ = other.width_;
        height_ = other.height_;
        format_ = other.format_;
        other.pool_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
        other.format_ = -1;
    }
    return *this;
}

MediaFrame MediaFramePool::Acquire(int width, int height, int format, double pts) {
    if (!pool_ || width != width_ || height != height_ || format != format_) {
        av_buffer_pool_uninit(&pool_);
        int size = av_image_get_buffer_size(static_cast<AVPixelFormat>(format), width, height,
                                            kFramePoolAlign);
        if (size < 0) return MediaFrame();
        pool_ = av_buffer_pool_init(static_cast<size_t>(size), nullptr);
        if (!pool_) return MediaFrame();
        width_ = width;
        height_ = height;
        format_ = format;
    }

    AVBufferRef* buf = av_buffer_pool_get(pool_);
    if (!buf) return MediaFrame();

    MediaFrame mf;
    AVFrame* frame = mf.frame_.get();
    frame->format = format;
    frame->width = width;
    frame->height = height;
    if (av_image_fill_arrays(frame->data, frame->linesize, buf->data,
                             static_cast<AVPixelFormat>(format), width, height,
                             kFramePoolAlign) < 0) {
        av_buffer_unref(&buf);
        return MediaFrame();
    }
    frame->buf[0] = buf;  // frame now owns this reference
    mf.pts_ = pts;
    mf.type_ = MediaType::kVideo;
    return mf;
}

}  // namespace mvp
