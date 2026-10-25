#include "mvp/source_probe.h"

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}
#include <spdlog/spdlog.h>

namespace mvp {

namespace {

void FillContainerMeta(AVFormatContext* fmt_ctx, SourceInfo& info) {
    if (fmt_ctx->duration != AV_NOPTS_VALUE) {
        info.duration = static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE;
    }
    if (fmt_ctx->iformat && fmt_ctx->iformat->name) {
        info.format_name = fmt_ctx->iformat->name;
    }
    info.bit_rate = fmt_ctx->bit_rate;
}

void FillVideoStream(AVStream* st, int index, SourceInfo& info) {
    AVCodecParameters* codecpar = st->codecpar;
    VideoStream vs;
    vs.index = index;
    const AVCodecDescriptor* desc = avcodec_descriptor_get(codecpar->codec_id);
    if (desc) vs.codec_name = desc->name;
    vs.width = codecpar->width;
    vs.height = codecpar->height;
    vs.frame_rate_num = st->avg_frame_rate.num;
    vs.frame_rate_den = st->avg_frame_rate.den;
    if (const char* name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(codecpar->format))) {
        vs.pixel_format = name;
    }
    vs.bit_rate = codecpar->bit_rate;
    info.video_streams.push_back(std::move(vs));
}

void FillAudioStream(AVStream* st, int index, SourceInfo& info) {
    AVCodecParameters* codecpar = st->codecpar;
    AudioStream as_;
    as_.index = index;
    const AVCodecDescriptor* desc = avcodec_descriptor_get(codecpar->codec_id);
    if (desc) as_.codec_name = desc->name;
    as_.sample_rate = codecpar->sample_rate;
    as_.channels = codecpar->ch_layout.nb_channels;
    char layout[64] = {};
    if (av_channel_layout_describe(&codecpar->ch_layout, layout, sizeof(layout)) > 0) {
        as_.channel_layout = layout;
    }
    if (const char* name = av_get_sample_fmt_name(static_cast<AVSampleFormat>(codecpar->format))) {
        as_.sample_format = name;
    }
    as_.bit_rate = codecpar->bit_rate;
    info.audio_streams.push_back(std::move(as_));
}

}  // namespace

SourceInfo SourceProbe::Probe(const std::string& filepath) {
    SourceInfo info;
    info.filepath = filepath;

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, filepath.c_str(), nullptr, nullptr) < 0) {
        SPDLOG_ERROR("SourceProbe: failed to open '{}'", filepath);
        return info;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        SPDLOG_ERROR("SourceProbe: failed to find stream info for '{}'", filepath);
        avformat_close_input(&fmt_ctx);
        return info;
    }

    FillContainerMeta(fmt_ctx, info);

    for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
        AVStream* st = fmt_ctx->streams[i];
        AVCodecParameters* codecpar = st->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            FillVideoStream(st, static_cast<int>(i), info);
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            FillAudioStream(st, static_cast<int>(i), info);
        }
    }

    avformat_close_input(&fmt_ctx);
    SPDLOG_INFO("SourceProbe: '{}' - {} video, {} audio, {:.2f}s",
                filepath, info.video_streams.size(),
                info.audio_streams.size(), info.duration);
    return info;
}

}  // namespace mvp
