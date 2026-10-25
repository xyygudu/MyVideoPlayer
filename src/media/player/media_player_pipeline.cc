// MediaPlayer::Impl, part 2: building/replacing the playback graph and the
// event thread that reacts to what the graph reports.
#include <algorithm>
#include <optional>

#include <spdlog/spdlog.h>

#include "nodes/audio_sink_node.h"
#include "nodes/color_effect_node.h"
#include "nodes/decoder_node.h"
#include "nodes/demux_node.h"
#include "nodes/transform_effect_node.h"
#include "nodes/video_sink_node.h"
#include "player/media_player_impl.h"
#include "sync/clock.h"

namespace mvp {
namespace {
// Provenance: ffplay VIDEO_PICTURE_QUEUE_SIZE / SAMPLE_QUEUE_SIZE. These depths
// exist to absorb decode jitter and let the sink look one frame ahead for A/V
// sync timing.
constexpr int kVideoFrameDepth = 3;
constexpr int kAudioFrameDepth = 9;

// A GPU that keeps losing its device would rebuild forever.
constexpr int kMaxRecoveries = 3;
constexpr std::chrono::seconds kRecoveryWindow{10};
}  // namespace

// --- Graph construction ---

bool MediaPlayer::Impl::BuildGraph() {
    // Precondition: info_ populated by Open(), stream indices selected.
    graph_ = std::make_unique<graph::MediaGraph>();

    // Decode and effects use the renderer's GPU device so frames stay on the
    // GPU until presented. A null device keeps the pipeline software.
    graph_->SetGpuDevice(video_renderer_.SharedGpuDevice());
    video_renderer_.SetZeroCopy(options_.zero_copy);

    auto* demux = static_cast<graph::DemuxNode*>(
        graph_->AddNode(std::make_unique<graph::DemuxNode>(
            info_.filepath, video_stream_index_, audio_stream_index_)));
    if (video_stream_index_ >= 0) {
        WireVideoBranch(demux->Outputs()[0]);
    }
    if (audio_stream_index_ >= 0) {
        int audio_port = (video_stream_index_ >= 0) ? 1 : 0;
        WireAudioBranch(demux->Outputs()[audio_port]);
    }

    if (!graph_->Open()) {
        SPDLOG_ERROR("MediaPlayer: graph Open failed");
        return false;
    }
    if (!graph_->Negotiate()) {
        SPDLOG_ERROR("MediaPlayer: graph Negotiate failed");
        return false;
    }
    if (!graph_->Prepare()) {
        SPDLOG_ERROR("MediaPlayer: graph Prepare failed");
        return false;
    }
    return true;
}

void MediaPlayer::Impl::WireVideoBranch(graph::OutputPort* demux_output) {
    auto* vdec = static_cast<graph::DecoderNode*>(
        graph_->AddNode(std::make_unique<graph::DecoderNode>(options_.hardware_decode)));
    auto* vsink = static_cast<graph::VideoSinkNode*>(
        graph_->AddNode(std::make_unique<graph::VideoSinkNode>()));
    vsink->SetRenderer(&video_renderer_);
    video_decoder_ = vdec;
    video_sink_ = vsink;

    graph_->Connect(demux_output, vdec->Inputs()[0], graph::LinkCapacity::ForPackets());
    graph::OutputPort* effect_out = WireVideoEffects(vdec);
    graph_->Connect(effect_out, vsink->Inputs()[0],
                    graph::LinkCapacity::ForFrames(kVideoFrameDepth));
}

void MediaPlayer::Impl::WireAudioBranch(graph::OutputPort* demux_output) {
    auto* adec = static_cast<graph::DecoderNode*>(
        graph_->AddNode(std::make_unique<graph::DecoderNode>()));
    auto* asink = static_cast<graph::AudioSinkNode*>(
        graph_->AddNode(std::make_unique<graph::AudioSinkNode>()));

    graph_->Connect(demux_output, adec->Inputs()[0], graph::LinkCapacity::ForPackets());
    graph_->Connect(adec->Outputs()[0], asink->Inputs()[0],
                    graph::LinkCapacity::ForFrames(kAudioFrameDepth));
}

graph::OutputPort* MediaPlayer::Impl::WireVideoEffects(graph::DecoderNode* vdec) {
    auto* transform = static_cast<graph::TransformEffectNode*>(graph_->AddNode(
        std::make_unique<graph::TransformEffectNode>(options_.gpu_effects)));
    auto* color = static_cast<graph::ColorEffectNode*>(graph_->AddNode(
        std::make_unique<graph::ColorEffectNode>(options_.gpu_effects)));

    graph_->Connect(vdec->Outputs()[0], transform->Inputs()[0],
                    graph::LinkCapacity::ForFrames(kVideoFrameDepth));
    graph_->Connect(transform->Outputs()[0], color->Inputs()[0],
                    graph::LinkCapacity::ForFrames(kVideoFrameDepth));

    effect_manager_.Register("transform", "Transform", transform);
    effect_manager_.Register("color", "Color", color);

    return color->Outputs()[0];
}

// --- Graph replacement ---

void MediaPlayer::Impl::TeardownGraphLocked() {
    effect_manager_.Clear();
    video_sink_ = nullptr;
    video_decoder_ = nullptr;
    if (graph_) {
        SPDLOG_DEBUG("MediaPlayer: stopping graph");
        graph_->Stop();
        // Nodes and their frames go before the SDL renderer they present on.
        graph_.reset();
    }
    eos_sinks_.clear();
    eos_epoch_ = -1;
    settled_epoch_ = -1;
    refresh_again_ = false;
    started_ = false;
}

bool MediaPlayer::Impl::RebuildLocked(bool recover_device) {
    const double position = ResumePositionLocked();
    const PlaybackState previous = state_;
    const bool was_started = started_;
    const std::vector<EffectInfo> effects = effect_manager_.Describe();
    TeardownGraphLocked();
    if (recover_device) {
        video_renderer_.RecoverDevice();
    }
    if (!BuildGraph()) {
        SPDLOG_ERROR("MediaPlayer: rebuilding the pipeline failed");
        state_ = PlaybackState::kError;
        return false;
    }
    RestoreEffectsLocked(effects);
    if (position > 0.0) {
        graph_->Seek(position);  // Before Start: the demuxer begins right there.
    }
    ResumeStateLocked(previous, was_started);
    SPDLOG_INFO("MediaPlayer: pipeline rebuilt at {:.3f}s", position);
    return true;
}

double MediaPlayer::Impl::ResumePositionLocked() const {
    double pts = 0.0;
    int serial = 0;
    if (video_sink_ && video_sink_->DisplayedPosition(&pts, &serial)) {
        return pts;  // Exactly the frame on screen.
    }
    IClock* clock = graph_ ? graph_->MasterClock() : nullptr;
    return clock ? clock->Get() : 0.0;
}

void MediaPlayer::Impl::RestoreEffectsLocked(const std::vector<EffectInfo>& effects) {
    for (const EffectInfo& effect : effects) {
        effect_manager_.SetEnabled(effect.effect_id, effect.enabled);
        for (const EffectParam& param : effect.params) {
            effect_manager_.SetParam(effect.effect_id, param.id, param.value);
        }
    }
}

void MediaPlayer::Impl::ResumeStateLocked(PlaybackState previous, bool was_started) {
    state_ = PlaybackState::kReady;
    if (!was_started) {
        return;  // Never played: stay ready, as after Open().
    }
    PlayLocked();
    if (previous != PlaybackState::kPlaying && state_ == PlaybackState::kPlaying) {
        // Paused, finished or failed before: show the frame there, paused.
        graph_->SetPaused(true);
        state_ = PlaybackState::kPaused;
    }
}

// Effects run upstream of the sink, so a change made while paused shows only
// once the frame on screen is decoded again through the new chain (mpv's
// refresh seek). One refresh decodes at a time; changes arriving meanwhile
// are folded into a single follow-up.
void MediaPlayer::Impl::RefreshLocked() {
    if (!video_sink_ ||
        (state_ != PlaybackState::kPaused && state_ != PlaybackState::kFinished)) {
        return;  // Playing frames already pass through the new settings.
    }
    if (settled_epoch_ != graph_->SeekEpoch()) {
        refresh_again_ = true;
        return;
    }
    double pts = 0.0;
    int serial = 0;
    if (!video_sink_->DisplayedPosition(&pts, &serial)) {
        return;
    }
    SPDLOG_DEBUG("MediaPlayer: refreshing the frame at {:.3f}s", pts);
    graph_->Seek(pts);
    eos_sinks_.clear();
}

void MediaPlayer::Impl::SettleEpochLocked(int serial) {
    settled_epoch_ = serial;
    if (refresh_again_) {
        refresh_again_ = false;
        RefreshLocked();
    }
}

// --- Event thread ---

void MediaPlayer::Impl::StartEventThread() {
    event_thread_ = std::thread([this] { EventLoop(); });
}

void MediaPlayer::Impl::EventLoop() {
    for (;;) {
        graph::MessageBus* bus = nullptr;
        {
            std::lock_guard lock(mutex_);
            if (!graph_) {
                return;
            }
            bus = &graph_->Bus();  // Re-read: a device-lost rebuild replaces it.
        }
        std::optional<graph::GraphMessage> msg = bus->Wait();
        if (!msg) {
            return;  // Shut down by Close/Open/SetOptions.
        }
        HandleMessage(*msg);
    }
}

bool MediaPlayer::Impl::StopEventThread() {
    if (!event_thread_.joinable()) {
        return true;
    }
    if (event_thread_.get_id() == std::this_thread::get_id()) {
        SPDLOG_ERROR("MediaPlayer: Open/Close/SetOptions from the finished callback is not "
                     "supported, ignored");
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        if (graph_) {
            graph_->Bus().Shutdown();
        }
    }
    event_thread_.join();  // Without mutex_: the thread may be waiting for it.
    return true;
}

void MediaPlayer::Impl::HandleMessage(const graph::GraphMessage& msg) {
    MediaPlayer::FinishedCallback notify;
    {
        std::lock_guard lock(mutex_);
        switch (msg.type) {
            case graph::GraphMessageType::kError:
                HandleErrorLocked(msg);
                return;
            case graph::GraphMessageType::kDeviceLost:
                HandleDeviceLostLocked();
                return;
            case graph::GraphMessageType::kFirstFrame:
                if (msg.serial == graph_->SeekEpoch()) {
                    SettleEpochLocked(msg.serial);
                }
                return;
            case graph::GraphMessageType::kEos:
                if (!HandleEosLocked(msg)) {
                    return;
                }
                notify = finished_cb_;
                break;
        }
    }
    // Outside the lock, so the callback may call back into the player.
    if (notify) {
        notify();
    }
}

bool MediaPlayer::Impl::HandleEosLocked(const graph::GraphMessage& msg) {
    // A seek after the sink reported makes its EOS stale.
    if (msg.serial != graph_->SeekEpoch()) {
        return false;
    }
    if (msg.source == video_sink_) {
        SettleEpochLocked(msg.serial);  // A seek past the last frame shows none.
        if (msg.serial != graph_->SeekEpoch()) {
            return false;  // Settling started a refresh, a new epoch.
        }
    }
    if (state_ != PlaybackState::kPlaying && state_ != PlaybackState::kPaused) {
        return false;
    }
    if (eos_epoch_ != msg.serial) {
        eos_sinks_.clear();
        eos_epoch_ = msg.serial;
    }
    eos_sinks_.insert(msg.source);
    if (static_cast<int>(eos_sinks_.size()) < sink_count_) {
        return false;
    }
    SPDLOG_INFO("MediaPlayer: playback finished");
    graph_->SetPaused(true);
    state_ = PlaybackState::kFinished;
    return true;
}

void MediaPlayer::Impl::HandleErrorLocked(const graph::GraphMessage& msg) {
    SPDLOG_ERROR("MediaPlayer: playback error from {}: {}",
                 msg.source ? msg.source->Name() : std::string("graph"), msg.detail);
    if (state_ == PlaybackState::kError) {
        return;
    }
    // Freeze output; the graph stays allocated until Close()/Open().
    graph_->SetPaused(true);
    state_ = PlaybackState::kError;
}

void MediaPlayer::Impl::HandleDeviceLostLocked() {
    const auto now = std::chrono::steady_clock::now();
    if (now - recovery_window_start_ > kRecoveryWindow) {
        recovery_window_start_ = now;
        recoveries_ = 0;
    }
    if (++recoveries_ > kMaxRecoveries) {
        HandleErrorLocked({graph::GraphMessageType::kError, nullptr, 0,
                           "GPU device lost repeatedly, giving up"});
        return;
    }
    // Runs on the event thread: EventLoop picks up the new graph's bus.
    SPDLOG_WARN("MediaPlayer: GPU device lost, rebuilding the pipeline on the new device");
    RebuildLocked(/*recover_device=*/true);
}

}  // namespace mvp
