#include "mvp/media_player.h"

#include <algorithm>

#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/hwcontext.h>
}

#include "clock.h"
#include "graph/media_graph.h"
#include "graph/port.h"
#include "nodes/demux_node.h"
#include "nodes/playback_graph_builder.h"
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
    void SetVideoFrameCallback(MediaPlayer::VideoFrameCallback cb) {
        video_frame_cb_ = std::move(cb);
    }
    void SetFinishedCallback(MediaPlayer::FinishedCallback cb) {
        finished_cb_ = std::move(cb);
    }

  private:
    bool BuildGraph(const std::string& filepath);
    void OnGraphEvent(graph::GraphEvent event);

    // Graph (owns all nodes)
    std::unique_ptr<graph::MediaGraph> graph_;

    // Clocks
    Clock audio_clock_;
    Clock video_clock_;

    // Video rendering
    VideoRenderer video_renderer_;
    void* window_handle_{nullptr};

    // State
    PlaybackState state_{PlaybackState::kIdle};
    double video_fps_{30.0};
    double duration_{0.0};   // Cached from source probe
    bool has_audio_{false};  // Whether the source has an audio stream
    int eos_count_{0};
    int sink_count_{0};

    // Callbacks
    MediaPlayer::VideoFrameCallback video_frame_cb_;
    MediaPlayer::FinishedCallback finished_cb_;
};

// --- Impl methods ---

bool MediaPlayer::Impl::Open(const std::string& filepath) {
    Close();

    if (!BuildGraph(filepath)) {
        state_ = PlaybackState::kError;
        return false;
    }

    state_ = PlaybackState::kReady;
    return true;
}

void MediaPlayer::Impl::Close() {
    if (graph_) {
        graph_->Stop();
        graph_.reset();
    }
    video_renderer_.Close();

    eos_count_ = 0;
    sink_count_ = 0;
    duration_ = 0.0;
    has_audio_ = false;

    audio_clock_.Reset();
    video_clock_.Reset();
    state_ = PlaybackState::kIdle;
}

void MediaPlayer::Impl::Play() {
    if (state_ == PlaybackState::kReady || state_ == PlaybackState::kPaused) {
        if (state_ == PlaybackState::kPaused) {
            audio_clock_.SetPaused(false);
            video_clock_.SetPaused(false);
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

    audio_clock_.SetPaused(true);
    video_clock_.SetPaused(true);
    graph_->SetPaused(true);
    state_ = PlaybackState::kPaused;
}

void MediaPlayer::Impl::Seek(double position_seconds) {
    if (!graph_ || state_ == PlaybackState::kIdle) return;

    // Graph coordinates the seek: flush all links, then broadcast a seek
    // command so each node resets its own state (demux repositions, decoder
    // drops to target, audio sink clears its buffer).
    graph_->Seek(position_seconds);

    audio_clock_.Reset(position_seconds);
    video_clock_.Reset(position_seconds);

    eos_count_ = 0;
    if (state_ == PlaybackState::kFinished) {
        state_ = PlaybackState::kPaused;
    }
}

PlaybackState MediaPlayer::Impl::State() const {
    return state_;
}

double MediaPlayer::Impl::Duration() const {
    return duration_;
}

double MediaPlayer::Impl::CurrentPosition() const {
    // AudioMaster: audio clock is the primary reference.
    return has_audio_ ? audio_clock_.Get() : video_clock_.Get();
}

double MediaPlayer::Impl::VideoFps() const {
    return video_fps_;
}

void MediaPlayer::Impl::NotifyWindowResized(int w, int h) {
    video_renderer_.Resize(w, h);
}

bool MediaPlayer::Impl::BuildGraph(const std::string& filepath) {
    graph_ = std::make_unique<graph::MediaGraph>();
    graph_->SetEventCallback(
        [this](graph::GraphEvent e) { OnGraphEvent(e); });

    // --- Source probe: discover stream topology before building the graph ---
    auto demux = std::make_unique<graph::DemuxNode>(filepath);
    std::vector<graph::StreamInfo> streams = demux->Probe();
    if (streams.empty()) {
        SPDLOG_ERROR("MediaPlayer: source probe found no streams");
        return false;
    }
    duration_ = streams.front().duration;
    has_audio_ = std::any_of(streams.begin(), streams.end(), [](const auto& s) {
        return s.type == MediaType::kAudio;
    });
    for (const auto& s : streams) {
        if (s.type == MediaType::kVideo) {
            const auto& enc = s.format.AsEncoded();
            video_fps_ = (enc.frame_rate.den > 0)
                             ? static_cast<double>(enc.frame_rate.num) /
                                   enc.frame_rate.den
                             : 30.0;
        }
    }

    auto* demux_node =
        static_cast<graph::DemuxNode*>(graph_->AddNode(std::move(demux)));
    if (!demux_node->Prepare()) {
        return false;
    }
    auto demux_outputs = demux_node->Outputs();

    // --- Build per-stream pipelines via the builder ---
    graph::PlaybackContext ctx;
    ctx.graph = graph_.get();
    ctx.renderer = &video_renderer_;
    ctx.audio_clock = &audio_clock_;
    ctx.video_clock = &video_clock_;
    ctx.window_handle = window_handle_;
    ctx.video_cb = video_frame_cb_;
    ctx.has_audio = has_audio_;
    graph::PlaybackGraphBuilder builder(ctx);

    // Streams and demux output ports share the same order (video, then audio).
    for (size_t i = 0; i < streams.size() && i < demux_outputs.size(); ++i) {
        graph::OutputPort* src = demux_outputs[i];
        if (streams[i].type == MediaType::kVideo) {
            builder.AddVideoPipeline(streams[i], src);
            sink_count_++;
        } else if (streams[i].type == MediaType::kAudio) {
            builder.AddAudioPipeline(streams[i], src);
            sink_count_++;
        }
    }

    // --- Graph lifecycle: Negotiate -> Prepare ---
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

void MediaPlayer::Impl::OnGraphEvent(graph::GraphEvent event) {
    if (event == graph::GraphEvent::kEos) {
        eos_count_++;
        if (eos_count_ >= sink_count_) {
            state_ = PlaybackState::kFinished;
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

void MediaPlayer::SetVideoFrameCallback(VideoFrameCallback cb) {
    impl_->SetVideoFrameCallback(std::move(cb));
}

void MediaPlayer::SetPlaybackFinishedCallback(FinishedCallback cb) {
    impl_->SetFinishedCallback(std::move(cb));
}

void MediaPlayer::SetFilter(const std::string& /*filter_desc*/) {
    // TODO: Phase 3 — Stop→Rebuild→Start with AVFilterNode
    SPDLOG_WARN("MediaPlayer::SetFilter not yet implemented (Phase 3)");
}

}  // namespace mvp
