#ifndef MVP_HW_ACCEL_CONTEXT_H_
#define MVP_HW_ACCEL_CONTEXT_H_

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

namespace mvp {

/// 硬件加速设备上下文管理。
/// 负责探测系统硬件解码能力、创建 AVHWDeviceContext，
/// 并提供 get_format 回调供 Decoder 注册。
class HWAccelContext {
  public:
    ~HWAccelContext();

    HWAccelContext(const HWAccelContext&) = delete;
    HWAccelContext& operator=(const HWAccelContext&) = delete;

    /// 尝试创建指定类型的硬件加速上下文。失败返回 nullptr（静默降级）。
    static std::unique_ptr<HWAccelContext> Create(AVHWDeviceType type);

    /// 获取设备引用（调用方用 av_buffer_ref 创建副本赋给 codec_ctx）。
    AVBufferRef* DeviceRef() const { return hw_device_ctx_; }

    /// 该加速类型对应的硬件 pixel format（如 AV_PIX_FMT_D3D11）。
    AVPixelFormat HWPixelFormat() const { return hw_pix_fmt_; }

    /// FFmpeg get_format 回调。通过 codec_ctx->opaque 获取 HWAccelContext*。
    static AVPixelFormat GetFormat(AVCodecContext* ctx,
                                  const AVPixelFormat* pix_fmts);

  private:
    HWAccelContext() = default;

    AVBufferRef* hw_device_ctx_ = nullptr;
    AVPixelFormat hw_pix_fmt_ = AV_PIX_FMT_NONE;
};

}  // namespace mvp

#endif  // MVP_HW_ACCEL_CONTEXT_H_
