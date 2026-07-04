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
#include "nodes/audio_sink_node.h"
#include "nodes/decoder_node.h"
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

    // Stream topology discovered by DemuxNode constructor (stream_index -> info).
    std::unordered_map<int, graph::StreamInfo> streams_; 

    // Clocks
    Clock audio_clock_;
    Clock video_clock_;

    // Video rendering
    VideoRenderer video_renderer_;
    void* window_handle_{nullptr};

    int window_width_{640};
    int window_height_{480};

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

    video_renderer_.Open(window_handle_, window_width_, window_height_); 

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
    window_width_ = w;
    window_height_ = h;
    video_renderer_.Resize(w, h);
}

bool MediaPlayer::Impl::BuildGraph(const std::string& filepath) {
    graph_ = std::make_unique<graph::MediaGraph>();
    graph_->SetEventCallback([this](graph::GraphEvent e) { OnGraphEvent(e); });

    std::unique_ptr<graph::DemuxNode> demux = std::make_unique<graph::DemuxNode>(filepath);
    std::unique_ptr<graph::DecoderNode> video_decoder = nullptr;
    std::unique_ptr<graph::VideoSinkNode> video_sink = nullptr;
    std::unique_ptr<graph::DecoderNode> audio_decoder = nullptr;
    std::unique_ptr<graph::AudioSinkNode> audio_sink = nullptr;

    streams_ = demux->StreamInfoMap();
    if (streams_.empty()) {
        SPDLOG_ERROR("MediaPlayer: source probe found no streams");
        return false;
    }

    for (const auto& s : streams_) {
        if (s.second.type == MediaType::kVideo && !video_decoder) {
            video_decoder = std::make_unique<graph::DecoderNode>();
            video_sink = std::make_unique<graph::VideoSinkNode>();
        } else if (s.second.type == MediaType::kAudio && !audio_decoder) {
            audio_decoder = std::make_unique<graph::DecoderNode>();
            audio_sink = std::make_unique<graph::AudioSinkNode>();
        }
    }

    auto* demux_node = static_cast<graph::DemuxNode*>(graph_->AddNode(std::move(demux)));
    graph::DecoderNode* video_decoder_node = nullptr;
    graph::DecoderNode* audio_decoder_node = nullptr;
    graph::VideoSinkNode* video_sink_node = nullptr;
    graph::AudioSinkNode* audio_sink_node = nullptr;

    if (video_decoder) {
        video_decoder_node = static_cast<graph::DecoderNode*>(graph_->AddNode(std::move(video_decoder)));
    }
    if (audio_decoder) {
        audio_decoder_node = static_cast<graph::DecoderNode*>(graph_->AddNode(std::move(audio_decoder)));
    }
    if (video_sink) {
        video_sink_node = static_cast<graph::VideoSinkNode*>(graph_->AddNode(std::move(video_sink)));
    }
    if (audio_sink) {
        audio_sink_node = static_cast<graph::AudioSinkNode*>(graph_->AddNode(std::move(audio_sink)));
    }
    
    for (const auto& s : streams_) {
        if (s.second.type == MediaType::kVideo) {
            const auto& enc = s.second.format.AsEncoded();
            video_fps_ = (enc.frame_rate.den > 0)
                             ? static_cast<double>(enc.frame_rate.num) /
                                   enc.frame_rate.den
                             : 30.0;
        }
    }
    duration_ = streams_.begin()->second.duration;
    has_audio_ = std::any_of(streams_.begin(), streams_.end(), [](const auto& s) {
        return s.second.type == MediaType::kAudio;
    });

    if (video_sink_node) {
        video_sink_node->SetRenderer(&video_renderer_);
        video_sink_node->SetAudioClock(&audio_clock_);
        video_sink_node->SetVideoClock(&video_clock_);
        video_sink_node->SetVideoFps(video_fps_);
        video_sink_node->SetFrameCallback(video_frame_cb_);
        video_sink_node->SetSyncMode(has_audio_
                          ? graph::VideoSinkNode::SyncMode::kAudioMaster
                          : graph::VideoSinkNode::SyncMode::kVideoMaster);
        video_sink_node->SetGraph(graph_.get());
    }

    if (audio_sink_node) {
        audio_sink_node->SetAudioClock(&audio_clock_);
        audio_sink_node->SetGraph(graph_.get());
    }

    if (video_decoder_node && video_sink_node) {
        if (demux_node->Outputs().size() > 0 && video_decoder_node->Inputs().size() > 0) {
            graph_->Connect(demux_node->Outputs()[0], video_decoder_node->Inputs()[0]);
        }
        if (video_decoder_node->Outputs().size() > 0 && video_sink_node->Inputs().size() > 0) {
            graph_->Connect(video_decoder_node->Outputs()[0], video_sink_node->Inputs()[0]);
        }
    }
    if (audio_decoder_node && audio_sink_node) {
        if (demux_node->Outputs().size() > 1 && audio_decoder_node->Inputs().size() > 0) {
            graph_->Connect(demux_node->Outputs()[1], audio_decoder_node->Inputs()[0]);
        }
        if (audio_decoder_node->Outputs().size() > 0 && audio_sink_node->Inputs().size() > 0) {
            graph_->Connect(audio_decoder_node->Outputs()[0], audio_sink_node->Inputs()[0]);
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
