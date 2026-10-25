#include "audio_resampler.h"

extern "C" {
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"

namespace mvp {

AudioResampler::AudioResampler(AVSampleFormat out_format, int out_rate,
                               const AVChannelLayout& out_layout)
    : out_format_(out_format), out_rate_(out_rate) {
    av_channel_layout_copy(&out_layout_, &out_layout);
}

AudioResampler::~AudioResampler() {
    swr_free(&swr_);
    av_channel_layout_uninit(&out_layout_);
    av_channel_layout_uninit(&in_layout_);
}

bool AudioResampler::SameInput(const AVFrame* in) const {
    return has_input_ && in->format == in_format_ && in->sample_rate == in_rate_ &&
           av_channel_layout_compare(&in->ch_layout, &in_layout_) == 0;
}

bool AudioResampler::EnsureConfigured(const AVFrame* in) {
    if (SameInput(in)) {
        return swr_ != nullptr;  // A failed configuration stays failed, logged once.
    }
    const bool reconfigure = has_input_;
    has_input_ = true;
    in_format_ = in->format;
    in_rate_ = in->sample_rate;
    av_channel_layout_uninit(&in_layout_);
    av_channel_layout_copy(&in_layout_, &in->ch_layout);
    swr_free(&swr_);

    AVChannelLayout layout{};
    if (av_channel_layout_check(&in->ch_layout)) {
        av_channel_layout_copy(&layout, &in->ch_layout);
    } else {
        av_channel_layout_default(&layout, in->ch_layout.nb_channels);
    }
    int ret = swr_alloc_set_opts2(&swr_, &out_layout_, out_format_, out_rate_, &layout,
                                  static_cast<AVSampleFormat>(in->format), in->sample_rate,
                                  0, nullptr);
    if (ret >= 0) {
        ret = swr_init(swr_);
    }
    av_channel_layout_uninit(&layout);
    if (ret < 0) {
        swr_free(&swr_);
        SPDLOG_ERROR("AudioResampler: cannot convert {} Hz, {} ch, format {}: {}", in->sample_rate,
                     in->ch_layout.nb_channels, in->format, AvErrorString(ret));
        return false;
    }
    if (reconfigure) {
        SPDLOG_INFO("AudioResampler: input changed to {} Hz, {} ch, format {}", in->sample_rate,
                    in->ch_layout.nb_channels, in->format);
    }
    return true;
}

bool AudioResampler::Convert(const AVFrame* in, AVFrame* out) {
    av_frame_unref(out);
    if (in && !EnsureConfigured(in)) {
        return false;
    }
    if (!swr_) {
        return true;  // Draining before any input: nothing is buffered.
    }
    const int in_samples = in ? in->nb_samples : 0;
    const int capacity = swr_get_out_samples(swr_, in_samples);
    if (capacity < 0) {
        SPDLOG_ERROR("AudioResampler: swr_get_out_samples failed: {}", AvErrorString(capacity));
        return false;
    }
    if (capacity == 0) {
        return true;
    }
    out->format = out_format_;
    out->sample_rate = out_rate_;
    out->nb_samples = capacity;
    int ret = av_channel_layout_copy(&out->ch_layout, &out_layout_);
    if (ret >= 0) {
        ret = av_frame_get_buffer(out, 0);
    }
    if (ret < 0) {
        SPDLOG_ERROR("AudioResampler: output allocation failed: {}", AvErrorString(ret));
        return false;
    }
    const int converted = swr_convert(swr_, out->extended_data, capacity,
                                      in ? in->extended_data : nullptr, in_samples);
    if (converted < 0) {
        SPDLOG_ERROR("AudioResampler: swr_convert failed: {}", AvErrorString(converted));
        av_frame_unref(out);
        return false;
    }
    out->nb_samples = converted;
    return true;
}

}  // namespace mvp
