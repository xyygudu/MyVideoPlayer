#include "frame_capture.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"

namespace mvp {

namespace {

// libswscale assumes BT.601 limited range for YUV input unless told otherwise.
void ApplySourceColorimetry(SwsContext* sws, const AVFrame* f) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format));
    if (!desc || (desc->flags & AV_PIX_FMT_FLAG_RGB)) {
        return;
    }
    const int* src_matrix = sws_getCoefficients(ResolvedColorspace(f));
    const int* dst_matrix = sws_getCoefficients(SWS_CS_DEFAULT);
    sws_setColorspaceDetails(sws, src_matrix, IsFullRangeFrame(f) ? 1 : 0, dst_matrix, 1, 0,
                             1 << 16, 1 << 16);
}

}  // namespace

bool ConvertToRgba(const MediaFrame& frame, std::vector<uint8_t>* rgba) {
    MediaFrame downloaded;
    const MediaFrame* src = &frame;
    if (frame.IsHardware()) {
        downloaded = TransferToSoftware(frame);
        src = &downloaded;
    }
    const AVFrame* f = src->IsValid() ? src->RawFrame() : nullptr;
    if (!f || f->width <= 0 || f->height <= 0) {
        SPDLOG_WARN("ConvertToRgba: no frame data to convert");
        return false;
    }
    SwsContext* sws = sws_getContext(f->width, f->height, WithoutJpegRange(f->format),
                                     f->width, f->height, AV_PIX_FMT_RGBA,
                                     SWS_BICUBIC | SWS_FULL_CHR_H_INT | SWS_ACCURATE_RND,
                                     nullptr, nullptr, nullptr);
    if (!sws) {
        SPDLOG_WARN("ConvertToRgba: no conversion from pixel format {}", f->format);
        return false;
    }
    ApplySourceColorimetry(sws, f);
    rgba->resize(static_cast<size_t>(f->width) * f->height * 4);
    uint8_t* dst[4] = {rgba->data(), nullptr, nullptr, nullptr};
    const int dst_stride[4] = {f->width * 4, 0, 0, 0};
    const int rows = sws_scale(sws, f->data, f->linesize, 0, f->height, dst, dst_stride);
    sws_freeContext(sws);
    return rows == f->height;
}

}  // namespace mvp
