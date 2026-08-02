#include "mvp/media_player.h"

#include <algorithm>
#include <limits>

#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/hwcontext.h>
}

#include "clock.h"
#include "effect_manager.h"
#include "graph/media_graph.h"
#include "graph/port.h"
#include "mvp/source_info.h"
#include "mvp/source_probe.h"
#include "nodes/demux_node.h"
#include "nodes/audio_sink_node.h"
#include "nodes/color_effect_node.h"
#include "nodes/decoder_node.h"
#include "nodes/transform_effect_node.h"
#include "nodes/video_sink_node.h"
#include "nodes/audio_sink_node.h"
#include "video_renderer.h"

namespace mvp {

class MediaPlayer::Impl {
  public:
    Impl() = default;
    ~Impl() { Close(); }

    bool Open(const std::string& filepath);
    void Close();
    void Play();
    void Pause();
    void Seek(double position_seconds);
    PlaybackState State() const;
    double Duration() const;
    double CurrentPosition() const;
    double VideoFps() const;

    void SetWindowHandle(void* handle) { window_handle_ = handle; }
    void NotifyWindowResized(int w, int h);
    void SetFinishedCallback(MediaPlayer::FinishedCallback cb) {
        finished_cb_ = std::move(cb);
    }

    std::vector<EffectInfo> EffectInfos() const { return effect_manager_.Describe(); }
    void SetEffectParam(const std::string& effect_id, const std::string& param_id,
                        EffectParamValue value) {
        effect_manager_.SetParam(effect_id, param_id, value);
    }
    void SetEffectEnabled(const std::string& effect_id, bool enabled) {
        effect_manager_.SetEnabled(effect_id, enabled);
    }

  private:
    bool BuildGraph();
    void OnGraphEvent(graph::GraphEvent event);

    /// Creates TransformEffectNode -> ColorEffectNode, wires
    /// Decoder -> Transform -> Color, and registers both with
    /// effect_manager_. Returns the output port BuildGraph should connect
    /// to the video sink. Split out so BuildGraph itself stays ≤ 40 lines.
    graph::OutputPort* WireVideoEffects(graph::DecoderNode* vdec);

    // Source meta (populated by SourceProbe in Open, consumed by BuildGraph)
    SourceInfo info_;

    // Selected stream indices (default: first of each type, -1 = none)
    int video_stream_index_{-1};
    int audio_stream_index_{-1};
    int sink_count_{0};      // number of active sinks (for EOS counting)
    int eos_count_{0};

    // Graph (owns all nodes)
    std::unique_ptr<graph::MediaGraph> graph_;

    // Indexes the effect nodes WireVideoEffects wires into the graph, for
    // EffectInfos()/SetEffectParam()/SetEffectEnabled(). Non-owning — must
    // be cleared before graph_ is reset (see Close()).
    EffectManager effect_manager_;

    // Video rendering
    VideoRenderer video_renderer_;
    void* window_handle_{nullptr};

    int window_width_{640};
    int window_height_{480};

    // State
    PlaybackState state_{PlaybackState::kIdle};

    // Callbacks
    MediaPlayer::FinishedCallback finished_cb_;
};

// --- Impl methods ---

bool MediaPlayer::Impl::Open(const std::string& filepath) {
    Close();

    // 1. Probe source independently of Graph construction.
    info_ = SourceProbe::Probe(filepath);
    if (info_.video_streams.empty() && info_.audio_streams.empty()) {
        SPDLOG_ERROR("MediaPlayer: source probe found no streams");
        state_ = PlaybackState::kError;
        return false;
    }

    // 2. Select default streams (first of each type).
    video_stream_index_ = info_.video_streams.empty() ? -1 : info_.video_streams[0].index;
    audio_stream_index_ = info_.audio_streams.empty() ? -1 : info_.audio_streams[0].index;
    sink_count_ = (video_stream_index_ >= 0 ? 1 : 0) + (audio_stream_index_ >= 0 ? 1 : 0);

    // 3. Build the playback graph.
    if (!BuildGraph()) {
        state_ = PlaybackState::kError;
        return false;
    }

    video_renderer_.Open(window_handle_, window_width_, window_height_);

    state_ = PlaybackState::kReady;
    return true;
}

void MediaPlayer::Impl::Close() {
    effect_manager_.Clear();
    if (graph_) {
        graph_->Stop();
        graph_.reset();
    }
    video_renderer_.Close();

    info_ = SourceInfo{};
    video_stream_index_ = -1;
    audio_stream_index_ = -1;
    eos_count_ = 0;
    sink_count_ = 0;

    state_ = PlaybackState::kIdle;
}

void MediaPlayer::Impl::Play() {
    if (state_ == PlaybackState::kReady || state_ == PlaybackState::kPaused) {
        if (state_ == PlaybackState::kPaused) {
            graph_->SetPaused(false);
            state_ = PlaybackState::kPlaying;
            return;
        }

        // First play from Ready
        if (!graph_->Start()) {
            state_ = PlaybackState::kError;
            SPDLOG_ERROR("MediaPlayer: graph Start failed");
            return;
        }
        state_ = PlaybackState::kPlaying;
    } else if (state_ == PlaybackState::kFinished) {
        // Replay from beginning
        Seek(0.0);
        Play();
    }
}

void MediaPlayer::Impl::Pause() {
    if (state_ != PlaybackState::kPlaying) return;

    graph_->SetPaused(true);
    state_ = PlaybackState::kPaused;
}

void MediaPlayer::Impl::Seek(double position_seconds) {
    if (!graph_ || state_ == PlaybackState::kIdle) return;

    // Graph coordinates the seek: flush all links, broadcast a seek command
    // so each node resets its own state (demux repositions, decoder drops to
    // target, audio sink clears its buffer), then reposition every clock.
    graph_->Seek(position_seconds);

    eos_count_ = 0;
    if (state_ == PlaybackState::kFinished) {
        state_ = PlaybackState::kPaused;
    }
}

PlaybackState MediaPlayer::Impl::State() const {
    return state_;
}

double MediaPlayer::Impl::Duration() const {
    return info_.duration;
}

double MediaPlayer::Impl::CurrentPosition() const {
    IClock* clock = graph_ ? graph_->MasterClock() : nullptr;
    return clock ? clock->Get() : 0.0;
}

double MediaPlayer::Impl::VideoFps() const {
    if (info_.video_streams.empty()) return 30.0;
    const auto& vs = info_.video_streams[0];
    return (vs.frame_rate_den > 0) ? static_cast<double>(vs.frame_rate_num) / vs.frame_rate_den : 30.0;
}

void MediaPlayer::Impl::NotifyWindowResized(int w, int h) {
    // Recorded for the renderer's initial Open; the live update goes through
    // the graph so only the render thread touches the renderer.
    window_width_ = w;
    window_height_ = h;
    if (graph_) {
        graph_->SendCommand({graph::CommandType::kResize, 0.0, w, h});
    }
}

bool MediaPlayer::Impl::BuildGraph() {
    // Precondition: info_ populated by Open(), stream indices selected.

    // 1. Create graph and nodes.
    graph_ = std::make_unique<graph::MediaGraph>();
    graph_->SetEventCallback([this](graph::GraphEvent e) { OnGraphEvent(e); });

    auto* demux = static_cast<graph::DemuxNode*>(
        graph_->AddNode(std::make_unique<graph::DemuxNode>(
            info_.filepath, video_stream_index_, audio_stream_index_)));

    graph::DecoderNode* vdec = nullptr;
    graph::VideoSinkNode* vsink = nullptr;
    if (video_stream_index_ >= 0) {
        vdec = static_cast<graph::DecoderNode*>(graph_->AddNode(std::make_unique<graph::DecoderNode>()));
        vsink = static_cast<graph::VideoSinkNode*>(graph_->AddNode(std::make_unique<graph::VideoSinkNode>()));
    }

    graph::DecoderNode* adec = nullptr;
    graph::AudioSinkNode* asink = nullptr;
    if (audio_stream_index_ >= 0) {
        adec = static_cast<graph::DecoderNode*>(graph_->AddNode(std::make_unique<graph::DecoderNode>()));
        asink = static_cast<graph::AudioSinkNode*>(graph_->AddNode(std::make_unique<graph::AudioSinkNode>()));
    }

    // 2. Configure sink nodes.
    if (vsink) {
        vsink->SetRenderer(&video_renderer_);
    }

    // 3. Wire pipeline (inline).
    if (vdec && vsink) {
        graph_->Connect(demux->Outputs()[0], vdec->Inputs()[0], {15 * 1024 * 1024, 256});
        graph::OutputPort* effect_out = WireVideoEffects(vdec);
        graph_->Connect(effect_out, vsink->Inputs()[0], {std::numeric_limits<int64_t>::max(), 3});
    }
    if (adec && asink) {
        int audio_port = (video_stream_index_ >= 0) ? 1 : 0;
        graph_->Connect(demux->Outputs()[audio_port], adec->Inputs()[0], {15 * 1024 * 1024, 256});
        graph_->Connect(adec->Outputs()[0], asink->Inputs()[0], {std::numeric_limits<int64_t>::max(), 9});
    }

    // 4. Finalize.
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

graph::OutputPort* MediaPlayer::Impl::WireVideoEffects(graph::DecoderNode* vdec) {
    auto* transform = static_cast<graph::TransformEffectNode*>(
        graph_->AddNode(std::make_unique<graph::TransformEffectNode>()));
    auto* color = static_cast<graph::ColorEffectNode*>(
        graph_->AddNode(std::make_unique<graph::ColorEffectNode>()));

    graph_->Connect(vdec->Outputs()[0], transform->Inputs()[0]);
    graph_->Connect(transform->Outputs()[0], color->Inputs()[0]);

    effect_manager_.Register("transform", "Transform", transform);
    effect_manager_.Register("color", "Color", color);

    return color->Outputs()[0];
}

void MediaPlayer::Impl::OnGraphEvent(graph::GraphEvent event) {
    if (event == graph::GraphEvent::kEos) {
        eos_count_++;
        if (eos_count_ >= sink_count_) {
            state_ = PlaybackState::kFinished;
            graph_->SetPaused(true);
            if (finished_cb_) finished_cb_();
        }
    } else if (event == graph::GraphEvent::kError) {
        state_ = PlaybackState::kError;
    }
}

// --- MediaPlayer public interface (delegates to Impl) ---

MediaPlayer::MediaPlayer() : impl_(std::make_unique<Impl>()) {}
MediaPlayer::~MediaPlayer() = default;

bool MediaPlayer::Open(const std::string& filepath) {
    return impl_->Open(filepath);
}

void MediaPlayer::Close() { impl_->Close(); }
void MediaPlayer::Play() { impl_->Play(); }
void MediaPlayer::Pause() { impl_->Pause(); }
void MediaPlayer::Seek(double position_seconds) { impl_->Seek(position_seconds); }

PlaybackState MediaPlayer::State() const { return impl_->State(); }
double MediaPlayer::Duration() const { return impl_->Duration(); }
double MediaPlayer::CurrentPosition() const { return impl_->CurrentPosition(); }
double MediaPlayer::VideoFps() const { return impl_->VideoFps(); }
bool MediaPlayer::IsPlaying() const {
    return impl_->State() == PlaybackState::kPlaying;
}

void MediaPlayer::SetWindowHandle(void* native_handle) {
    impl_->SetWindowHandle(native_handle);
}

void MediaPlayer::NotifyWindowResized(int width, int height) {
    impl_->NotifyWindowResized(width, height);
}

void MediaPlayer::SetPlaybackFinishedCallback(FinishedCallback cb) {
    impl_->SetFinishedCallback(std::move(cb));
}

std::vector<EffectInfo> MediaPlayer::EffectInfos() const {
    return impl_->EffectInfos();
}

void MediaPlayer::SetEffectParam(const std::string& effect_id, const std::string& param_id,
                                 EffectParamValue value) {
    impl_->SetEffectParam(effect_id, param_id, value);
}

void MediaPlayer::SetEffectEnabled(const std::string& effect_id, bool enabled) {
    impl_->SetEffectEnabled(effect_id, enabled);
}

}  // namespace mvp
