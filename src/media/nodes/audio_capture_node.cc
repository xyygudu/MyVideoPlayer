#include "nodes/audio_capture_node.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}
#include <spdlog/spdlog.h>

#include "capture/audio_source.h"

namespace mvp::graph {
namespace {

// WASAPI buffers a second; reading every 10 ms keeps packets small and fresh.
constexpr double kPollInterval = 0.01;
constexpr double kReopenInterval = 1.0;

SampleFormat ToSampleFormat(int av_format) {
    switch (av_format) {
        case AV_SAMPLE_FMT_S16:
            return SampleFormat::kS16;
        case AV_SAMPLE_FMT_S32:
            return SampleFormat::kS32;
        case AV_SAMPLE_FMT_FLT:
            return SampleFormat::kFloat;
        case AV_SAMPLE_FMT_S16P:
            return SampleFormat::kS16Planar;
        case AV_SAMPLE_FMT_FLTP:
            return SampleFormat::kFloatPlanar;
        default:
            return SampleFormat::kUnknown;
    }
}

}  // namespace

AudioCaptureNode::AudioCaptureNode(AudioDeviceKind kind, std::string device_id)
    : kind_(kind),
      device_id_(std::move(device_id)),
      name_(kind == AudioDeviceKind::kOutput ? "AudioCaptureNode(system)" : "AudioCaptureNode(mic)") {}

AudioCaptureNode::~AudioCaptureNode() { Stop(); }

bool AudioCaptureNode::OpenDevice(MediaFormat* format) {
    source_ = capture::CreateAudioSource(kind_, device_id_);
    if (!source_->Open()) {
        SPDLOG_ERROR("{}: cannot open the audio device", name_);
        return false;
    }
    *format = MediaFormat::Audio(source_->SampleRate(), source_->Channels(),
                                 ToSampleFormat(source_->SampleFormat()));
    return true;
}

void AudioCaptureNode::CloseDevice() {
    if (source_) {
        source_->Close();
        source_.reset();
    }
}

void AudioCaptureNode::CaptureStep() {
    if (device_lost_) {
        if (!source_->Open()) {
            SleepFor(kReopenInterval);
            return;
        }
        SPDLOG_INFO("{}: audio device back", name_);
        device_lost_ = false;
    }
    if (!ReadPackets()) {
        SPDLOG_WARN("{}: audio device lost, retrying every {} s", name_, kReopenInterval);
        source_->Close();
        device_lost_ = true;
        SleepFor(kReopenInterval);
        return;
    }
    SleepFor(kPollInterval);
}

bool AudioCaptureNode::ReadPackets() {
    return source_->Read([this](AVFrame* frame, double age_seconds) {
        // Data captured while paused belongs to no running time: Emit drops it.
        Emit(MediaBuffer(MediaFrame(frame), Timestamp{Now() - age_seconds}));
    });
}

}  // namespace mvp::graph
