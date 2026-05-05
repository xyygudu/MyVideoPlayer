#include "hw_accel_context.h"

extern "C" {
#include <libavutil/pixdesc.h>
}
#include <spdlog/spdlog.h>

namespace mvp {

HWAccelContext::~HWAccelContext() {
    if (hw_device_ctx_) {
        av_buffer_unref(&hw_device_ctx_);
    }
}

std::unique_ptr<HWAccelContext> HWAccelContext::Create(AVHWDeviceType type) {
    // 查找该加速类型对应的硬件 pixel format
    AVPixelFormat hw_fmt = AV_PIX_FMT_NONE;
    const AVCodec* dummy = nullptr;
    void* iter = nullptr;
    while ((dummy = av_codec_iterate(&iter))) {
        if (!av_codec_is_decoder(dummy)) continue;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(dummy, i);
            if (!config) break;
            if (config->device_type == type &&
                (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
                hw_fmt = config->pix_fmt;
                break;
            }
        }
        if (hw_fmt != AV_PIX_FMT_NONE) break;
    }

    if (hw_fmt == AV_PIX_FMT_NONE) {
        SPDLOG_WARN("HWAccel: no hw pixel format found for device type {}",
                    av_hwdevice_get_type_name(type));
        return nullptr;
    }

    // 创建硬件设备上下文
    AVBufferRef* device_ctx = nullptr;
    int ret = av_hwdevice_ctx_create(&device_ctx, type, nullptr, nullptr, 0);
    if (ret < 0) {
        SPDLOG_WARN("HWAccel: failed to create device context for {} ({})",
                    av_hwdevice_get_type_name(type), ret);
        return nullptr;
    }

    auto ctx = std::unique_ptr<HWAccelContext>(new HWAccelContext());
    ctx->hw_device_ctx_ = device_ctx;
    ctx->hw_pix_fmt_ = hw_fmt;

    SPDLOG_INFO("HWAccel: {} device created (pix_fmt={})",
                av_hwdevice_get_type_name(type),
                av_get_pix_fmt_name(ctx->hw_pix_fmt_) ? av_get_pix_fmt_name(ctx->hw_pix_fmt_) : "unknown");
    return ctx;
}

AVPixelFormat HWAccelContext::GetFormat(AVCodecContext* ctx,
                                       const AVPixelFormat* pix_fmts) {
    auto* hw_ctx = static_cast<HWAccelContext*>(ctx->opaque);
    if (!hw_ctx) return pix_fmts[0];

    // 在候选列表中查找硬件格式
    for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == hw_ctx->hw_pix_fmt_) {
            return *p;
        }
    }

    // 硬件格式不在候选列表中，回退软件格式
    SPDLOG_WARN("HWAccel: hw pix_fmt not offered, falling back to software");
    return pix_fmts[0];
}

}  // namespace mvp
