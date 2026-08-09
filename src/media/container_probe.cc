#include "mvp/container_probe.h"

#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <spdlog/spdlog.h>

namespace mvp {

namespace {

// Curated candidates, not an exhaustive enumeration of every FFmpeg codec —
// extend by appending a name here. A candidate not compiled into the local
// FFmpeg build is skipped silently, never treated as an error.
const std::vector<std::string> kVideoCandidates = {"libx264", "libx265", "libvpx-vp9"};
const std::vector<std::string> kAudioCandidates = {"aac", "libopus", "libmp3lame"};

const AVOutputFormat* ResolveOutputFormat(const std::string& container_or_path) {
    std::string synthetic = container_or_path;
    if (synthetic.find('.') == std::string::npos) {
        synthetic = "x." + synthetic;  // bare container name -> matchable extension
    }
    return av_guess_format(container_or_path.c_str(), synthetic.c_str(), nullptr);
}

// avformat_query_codec returns 1 (supported) / 0 (unsupported) / negative
// (unknown, per FFmpeg's own doc comment). Only an explicit 0 excludes a
// candidate — negative results stay in, so muxers without a codec_tag
// table aren't falsely narrowed; avformat_write_header's existing error
// log remains the fallback for whatever this can't determine.
bool ContainerMayStore(const AVOutputFormat* ofmt, AVCodecID codec_id) {
    return avformat_query_codec(ofmt, codec_id, FF_COMPLIANCE_NORMAL) != 0;
}

std::vector<std::string> ProbeCandidates(const AVOutputFormat* ofmt,
                                         const std::vector<std::string>& candidates) {
    std::vector<std::string> result;
    for (const std::string& name : candidates) {
        const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec) continue;  // not compiled into this FFmpeg build
        if (ContainerMayStore(ofmt, codec->id)) {
            result.push_back(name);
        }
    }
    return result;
}

std::string ResolveDefault(AVCodecID default_id, const std::vector<std::string>& probed) {
    if (default_id != AV_CODEC_ID_NONE) {
        const AVCodec* codec = avcodec_find_encoder(default_id);
        if (codec) {
            auto it = std::find(probed.begin(), probed.end(), std::string(codec->name));
            if (it != probed.end()) return *it;
        }
    }
    return probed.empty() ? std::string() : probed.front();
}

}  // namespace

ContainerCodecCaps ContainerProbe::Query(const std::string& container_or_path) {
    ContainerCodecCaps caps;
    const AVOutputFormat* ofmt = ResolveOutputFormat(container_or_path);
    if (!ofmt) {
        SPDLOG_WARN("ContainerProbe: cannot resolve container for '{}'", container_or_path);
        return caps;
    }

    caps.video_codecs = ProbeCandidates(ofmt, kVideoCandidates);
    caps.audio_codecs = ProbeCandidates(ofmt, kAudioCandidates);
    caps.default_video_codec = ResolveDefault(ofmt->video_codec, caps.video_codecs);
    caps.default_audio_codec = ResolveDefault(ofmt->audio_codec, caps.audio_codecs);
    return caps;
}

}  // namespace mvp
