// Platform-neutral half of mvp::CaptureDevices.
#include "mvp/capture_devices.h"

extern "C" {
#include <libavcodec/avcodec.h>
}
#include <spdlog/spdlog.h>

namespace mvp {
namespace {

// Hardware first: they barely load the CPU, which the capture itself needs.
constexpr const char* kH264Encoders[] = {"h264_nvenc", "h264_qsv", "h264_amf", "libx264"};

AVPixelFormat FirstPixelFormat(const AVCodec* codec) {
    const void* formats = nullptr;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &formats, nullptr) < 0 ||
        !formats) {
        return AV_PIX_FMT_YUV420P;
    }
    return *static_cast<const AVPixelFormat*>(formats);
}

// Being compiled in proves nothing for hardware encoders (no GPU, old
// driver): only opening a session does.
bool EncoderOpens(const char* name) {
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    AVCodecContext* context = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!context) {
        return false;
    }
    context->width = 1280;
    context->height = 720;
    context->time_base = {1, 30};
    context->framerate = {30, 1};
    context->pix_fmt = FirstPixelFormat(codec);
    const int ret = avcodec_open2(context, codec, nullptr);
    avcodec_free_context(&context);
    SPDLOG_INFO("CaptureDevices: encoder {} {}", name, ret >= 0 ? "available" : "unavailable");
    return ret >= 0;
}

}  // namespace

std::vector<std::string> CaptureDevices::VideoEncoders() {
    std::vector<std::string> encoders;
    for (const char* name : kH264Encoders) {
        if (EncoderOpens(name)) encoders.emplace_back(name);
    }
    return encoders;
}

}  // namespace mvp
