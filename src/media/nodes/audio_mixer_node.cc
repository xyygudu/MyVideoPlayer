#include "nodes/audio_mixer_node.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}
#include <spdlog/spdlog.h>

#include "graph/media_graph.h"

namespace mvp::graph {
namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kChunk = 1024;  // One AAC frame: the encoder needs no re-chunking
constexpr double kLatency = 0.15;
constexpr int64_t kTolerance = kSampleRate / 50;  // 20 ms of timestamp jitter
constexpr auto kPollInterval = std::chrono::milliseconds(5);
constexpr float kMaxGain = 4.0f;

AVChannelLayout StereoLayout() {
    AVChannelLayout layout{};
    av_channel_layout_default(&layout, kChannels);
    return layout;
}

void RaiseLevel(std::atomic<float>& level, float peak) {
    float current = level.load(std::memory_order_relaxed);
    while (peak > current && !level.compare_exchange_weak(current, peak, std::memory_order_relaxed)) {
    }
}

}  // namespace

AudioMixerNode::AudioMixerNode(int inputs)
    : output_port_(std::make_unique<OutputPort>(this)), timeline_(inputs, kChannels, kTolerance) {
    for (int i = 0; i < inputs; ++i) {
        auto input = std::make_unique<InputState>();
        input->port = std::make_unique<InputPort>(this);
        inputs_.push_back(std::move(input));
    }
}

AudioMixerNode::~AudioMixerNode() { Stop(); }

std::vector<InputPort*> AudioMixerNode::Inputs() {
    std::vector<InputPort*> ports;
    for (auto& input : inputs_) {
        ports.push_back(input->port.get());
    }
    return ports;
}

void AudioMixerNode::DeclareCaps() {
    FormatCaps caps;
    caps.media_type = MediaType::kAudio;
    for (auto& input : inputs_) {
        input->port->SetCaps(caps);
    }
    output_port_->SetCaps(caps);
}

bool AudioMixerNode::Negotiate() {
    for (auto& input : inputs_) {
        if (!input->port->IsConnected()) {
            SPDLOG_ERROR("AudioMixerNode: an input is not connected");
            return false;
        }
    }
    output_port_->SetFormat(MediaFormat::Audio(kSampleRate, kChannels, SampleFormat::kFloatPlanar));
    state_ = NodeState::kConfigured;
    return true;
}

bool AudioMixerNode::Prepare() {
    const AVChannelLayout stereo = StereoLayout();
    for (auto& input : inputs_) {
        input->resampler = std::make_unique<AudioResampler>(AV_SAMPLE_FMT_FLTP, kSampleRate, stereo);
        input->ended = false;
    }
    state_ = NodeState::kPrepared;
    return true;
}

bool AudioMixerNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("AudioMixerNode: Start called in invalid state {}", static_cast<int>(state_));
        return false;
    }
    running_ = true;
    thread_ = std::thread(&AudioMixerNode::MixLoop, this);
    state_ = NodeState::kRunning;
    return true;
}

void AudioMixerNode::Stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    state_ = NodeState::kIdle;
}

void AudioMixerNode::SetGain(int input, float gain) {
    inputs_[static_cast<size_t>(input)]->gain.store(std::clamp(gain, 0.0f, kMaxGain));
}

float AudioMixerNode::TakeLevel(int input) { return inputs_[static_cast<size_t>(input)]->level.exchange(0.0f); }

void AudioMixerNode::MixLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        PullInputs();
        const bool all_ended =
            std::all_of(inputs_.begin(), inputs_.end(), [](const auto& input) { return input->ended; });
        if (!EmitDueChunks(all_ended)) {
            return;  // Downstream gone: the graph is stopping
        }
        if (all_ended) {
            SPDLOG_INFO("AudioMixerNode: end of stream");
            output_port_->Push(MediaBuffer::MakeEos(graph_ ? graph_->SeekEpoch() : 0));
            return;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
}

void AudioMixerNode::PullInputs() {
    for (size_t i = 0; i < inputs_.size(); ++i) {
        InputState& input = *inputs_[i];
        while (!input.ended) {
            std::optional<MediaBuffer> buf = input.port->TryPull();
            if (!buf) {
                break;
            }
            if (HasFlag(buf->flags(), BufferFlags::kEos)) {
                FinishInput(i);
            } else {
                WriteFrame(i, *buf);
            }
        }
    }
}

void AudioMixerNode::WriteFrame(size_t input, const MediaBuffer& buf) {
    if (!buf.IsFrame() || !inputs_[input]->resampler->Convert(buf.AsFrame().RawFrame(), resampled_.get())) {
        return;  // The resampler logs its failures
    }
    if (resampled_->nb_samples == 0) {
        return;
    }
    // The resampler's tiny filter delay is absorbed as timestamp jitter.
    const auto start = static_cast<int64_t>(std::llround(buf.timestamp().pts * kSampleRate));
    timeline_.Write(static_cast<int>(input), reinterpret_cast<const float* const*>(resampled_->extended_data),
                    resampled_->nb_samples, start);
}

void AudioMixerNode::FinishInput(size_t input) {
    InputState& state = *inputs_[input];
    // Samples still held by the resampler continue the input's last write.
    if (state.resampler->Convert(nullptr, resampled_.get()) && resampled_->nb_samples > 0) {
        timeline_.Write(static_cast<int>(input), reinterpret_cast<const float* const*>(resampled_->extended_data),
                        resampled_->nb_samples, timeline_.BufferedEnd(static_cast<int>(input)));
    }
    state.ended = true;
}

int64_t AudioMixerNode::Deadline(bool flushing) const {
    IClock* clock = graph_ ? graph_->MasterClock() : nullptr;
    if (!clock) {
        // Without a time base, coverage alone decides; a flush takes what was written.
        return flushing ? FlushEnd(std::numeric_limits<int64_t>::min()) : std::numeric_limits<int64_t>::min();
    }
    const auto now = static_cast<int64_t>(std::floor(clock->Get() * kSampleRate));
    // A silent loopback device writes nothing, yet the stream lasts until it ended.
    return flushing ? FlushEnd(now) : now - static_cast<int64_t>(kLatency * kSampleRate);
}

int64_t AudioMixerNode::FlushEnd(int64_t ended_at) const {
    int64_t end = std::max(timeline_.ReadPosition(), ended_at);
    for (size_t i = 0; i < inputs_.size(); ++i) {
        end = std::max(end, timeline_.BufferedEnd(static_cast<int>(i)));
    }
    return end + kChunk - 1;  // The last chunk is padded with silence
}

bool AudioMixerNode::ChunkDue(int64_t chunk_end, int64_t deadline) const {
    if (chunk_end <= deadline) {
        return true;
    }
    bool any_running = false;
    for (size_t i = 0; i < inputs_.size(); ++i) {
        if (inputs_[i]->ended) {
            continue;
        }
        any_running = true;
        if (timeline_.BufferedEnd(static_cast<int>(i)) < chunk_end) {
            return false;
        }
    }
    return any_running;
}

bool AudioMixerNode::EmitDueChunks(bool flushing) {
    const int64_t deadline = Deadline(flushing);
    while (ChunkDue(timeline_.ReadPosition() + kChunk, deadline)) {
        if (!EmitChunk()) {
            return false;
        }
    }
    return true;
}

bool AudioMixerNode::EmitChunk() {
    AVFramePtr frame;
    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->sample_rate = kSampleRate;
    frame->nb_samples = kChunk;
    av_channel_layout_default(&frame->ch_layout, kChannels);
    if (av_frame_get_buffer(frame.get(), 0) < 0) {
        SPDLOG_ERROR("AudioMixerNode: out of memory for a mix chunk");
        return false;
    }
    std::vector<float> gains(inputs_.size());
    std::vector<float> peaks(inputs_.size());
    for (size_t i = 0; i < inputs_.size(); ++i) {
        gains[i] = inputs_[i]->gain.load(std::memory_order_relaxed);
    }
    const double pts = static_cast<double>(timeline_.ReadPosition()) / kSampleRate;
    timeline_.MixNext(reinterpret_cast<float* const*>(frame->extended_data), kChunk, gains.data(), peaks.data());
    for (size_t i = 0; i < inputs_.size(); ++i) {
        RaiseLevel(inputs_[i]->level, peaks[i]);
    }
    MediaBuffer buf(MediaFrame(frame.get()), Timestamp{pts});
    buf.set_serial(graph_ ? graph_->SeekEpoch() : 0);
    return output_port_->Push(std::move(buf));
}

}  // namespace mvp::graph
