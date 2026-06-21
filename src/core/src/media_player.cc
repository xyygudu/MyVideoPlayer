#include "mvp/media_player.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavformat/avformat.h>
}

#include "clock.h"
#include "graph/media_graph.h"
#include "hw_accel_context.h"
#include "nodes/audio_sink_node.h"
#include "nodes/decoder_node.h"
#include "nodes/demux_node.h"
#include "nodes/video_sink_node.h"
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

    // Graph
    std::unique_ptr<graph::MediaGraph> graph_;

    // Node pointers (non-owning, owned by graph_)
    graph::DemuxNode* demux_node_{nullptr};
    graph::DecoderNode* video_decoder_{nullptr};
    graph::DecoderNode* audio_decoder_{nullptr};
    graph::VideoSinkNode* video_sink_{nullptr};
    graph::AudioSinkNode* audio_sink_{nullptr};

    // Clocks
    Clock audio_clock_;
    Clock video_clock_;

    // HW acceleration
    std::unique_ptr<HWAccelContext> hw_accel_;

    // Video rendering
    VideoRenderer video_renderer_;
    void* window_handle_{nullptr};

    // State
    PlaybackState state_{PlaybackState::kIdle};
    double video_fps_{30.0};
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
    hw_accel_.reset();

    demux_node_ = nullptr;
    video_decoder_ = nullptr;
    audio_decoder_ = nullptr;
    video_sink_ = nullptr;
    audio_sink_ = nullptr;
    eos_count_ = 0;
    sink_count_ = 0;

    audio_clock_.Reset();
    video_clock_.Reset();
    state_ = PlaybackState::kIdle;
}

void MediaPlayer::Impl::Play() {
    if (state_ == PlaybackState::kReady || state_ == PlaybackState::kPaused) {
        if (state_ == PlaybackState::kPaused) {
            audio_clock_.SetPaused(false);
            video_clock_.SetPaused(false);
            if (audio_sink_) audio_sink_->SetPaused(false);
            if (video_sink_) video_sink_->SetPaused(false);
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
    if (audio_sink_) audio_sink_->SetPaused(true);
    if (video_sink_) video_sink_->SetPaused(true);
    state_ = PlaybackState::kPaused;
}

void MediaPlayer::Impl::Seek(double position_seconds) {
    if (!graph_ || state_ == PlaybackState::kIdle) return;

    // 1. Flush all links and node internal state
    graph_->Flush();

    // 2. Clear SDL audio buffer
    if (audio_sink_) audio_sink_->FlushSdlBuffer();

    // 3. Tell decoder to drop until seek target
    if (video_decoder_) video_decoder_->SetDropUntilPts(position_seconds);

    // 4. Request seek on demux node
    if (demux_node_) demux_node_->RequestSeek(position_seconds);

    // 5. Reset clocks
    audio_clock_.Reset(position_seconds);
    video_clock_.Reset(position_seconds);

    // 6. Reset EOS count (we're no longer at end)
    eos_count_ = 0;
    if (state_ == PlaybackState::kFinished) {
        state_ = PlaybackState::kPaused;
    }
}

PlaybackState MediaPlayer::Impl::State() const {
    return state_;
}

double MediaPlayer::Impl::Duration() const {
    return demux_node_ ? demux_node_->Duration() : 0.0;
}

double MediaPlayer::Impl::CurrentPosition() const {
    // AudioMaster: use audio clock as primary
    if (audio_sink_) {
        return audio_clock_.Get();
    }
    return video_clock_.Get();
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

    // --- DemuxNode ---
    auto demux = std::make_unique<graph::DemuxNode>();
    graph::NodeConfig demux_cfg;
    demux_cfg.file_path = filepath;
    demux->Configure(demux_cfg);

    // Negotiate and Prepare demux to discover streams
    demux->Negotiate();
    if (!demux->Prepare()) {
        return false;
    }

    demux_node_ = static_cast<graph::DemuxNode*>(graph_->AddNode(std::move(demux)));

    // Access AVFormatContext streams via DemuxNode ports.
    // DemuxNode creates ports: [0]=video (if exists), [1]=audio (if exists)
    auto demux_outputs = demux_node_->Outputs();
    int video_port_idx = -1;
    int audio_port_idx = -1;

    if (demux_node_->VideoStreamIndex() >= 0) {
        video_port_idx = 0;
    }
    if (demux_node_->AudioStreamIndex() >= 0) {
        audio_port_idx = (video_port_idx >= 0) ? 1 : 0;
    }

    // We need access to AVStream* for decoder/audio setup.
    // This is a controlled internal coupling — DemuxNode exposes
    // stream info implicitly through its format_ctx_ (set during Prepare).
    // For decoder setup, we re-open the format context info.
    // Actually, we can use avformat API since DemuxNode exposes nothing beyond
    // ports. Better approach: open a temporary format context just for stream info.
    // BUT this duplicates work. Instead, let's expose stream access on DemuxNode.

    // We'll access the format context through the DemuxNode's internal state.
    // Since DemuxNode is our code, we add a helper method.
    // For now, we'll re-open briefly to get AVStream* for decoder config.
    // This is a pragmatic decision: DemuxNode is internal code we control.

    AVFormatContext* fmt_ctx = nullptr;
    avformat_open_input(&fmt_ctx, filepath.c_str(), nullptr, nullptr);
    avformat_find_stream_info(fmt_ctx, nullptr);

    // --- Try hardware acceleration ---
    hw_accel_ = HWAccelContext::Create(AV_HWDEVICE_TYPE_D3D11VA);

    // --- Video pipeline ---
    if (video_port_idx >= 0) {
        AVStream* vstream = fmt_ctx->streams[demux_node_->VideoStreamIndex()];

        auto vdecoder = std::make_unique<graph::DecoderNode>();
        graph::NodeConfig dec_cfg;
        vdecoder->Configure(dec_cfg);
        vdecoder->SetStream(vstream);
        if (hw_accel_) vdecoder->SetHWAccel(hw_accel_.get());
        vdecoder->Negotiate();
        if (!vdecoder->Prepare()) {
            avformat_close_input(&fmt_ctx);
            return false;
        }

        video_fps_ = (vstream->avg_frame_rate.den > 0)
                         ? av_q2d(vstream->avg_frame_rate)
                         : 30.0;

        video_decoder_ = static_cast<graph::DecoderNode*>(
            graph_->AddNode(std::move(vdecoder)));

        // VideoSinkNode
        auto vsink = std::make_unique<graph::VideoSinkNode>();
        vsink->Configure({});
        vsink->SetRenderer(&video_renderer_);
        vsink->SetAudioClock(&audio_clock_);
        vsink->SetVideoClock(&video_clock_);
        vsink->SetVideoFps(video_fps_);
        vsink->SetGraph(graph_.get());
        if (video_frame_cb_) vsink->SetFrameCallback(video_frame_cb_);

        // Determine sync mode
        if (audio_port_idx >= 0) {
            vsink->SetSyncMode(graph::VideoSinkNode::SyncMode::kAudioMaster);
        } else {
            vsink->SetSyncMode(graph::VideoSinkNode::SyncMode::kVideoMaster);
        }

        // Open video renderer
        int w = vstream->codecpar->width;
        int h = vstream->codecpar->height;
        if (window_handle_) {
            video_renderer_.Open(window_handle_, w, h);
        }

        vsink->Negotiate();
        vsink->Prepare();
        video_sink_ = static_cast<graph::VideoSinkNode*>(
            graph_->AddNode(std::move(vsink)));

        // Connect: DemuxNode[video_port] → DecoderNode → VideoSinkNode
        // Packet link: large capacity (256) to avoid DemuxNode backpressure
        // Frame link: small capacity (8) for low-latency rendering
        graph_->Connect(demux_outputs[video_port_idx],
                        video_decoder_->Inputs()[0], 256);
        graph_->Connect(video_decoder_->Outputs()[0],
                        video_sink_->Inputs()[0], 8);
        sink_count_++;
    }

    // --- Audio pipeline ---
    if (audio_port_idx >= 0) {
        AVStream* astream = fmt_ctx->streams[demux_node_->AudioStreamIndex()];

        auto adecoder = std::make_unique<graph::DecoderNode>();
        graph::NodeConfig dec_cfg;
        adecoder->Configure(dec_cfg);
        adecoder->SetStream(astream);
        adecoder->Negotiate();
        if (!adecoder->Prepare()) {
            avformat_close_input(&fmt_ctx);
            return false;
        }

        audio_decoder_ = static_cast<graph::DecoderNode*>(
            graph_->AddNode(std::move(adecoder)));

        // AudioSinkNode
        auto asink = std::make_unique<graph::AudioSinkNode>();
        asink->Configure({});
        asink->SetStream(astream);
        asink->SetAudioClock(&audio_clock_);
        asink->SetGraph(graph_.get());
        asink->Negotiate();
        if (!asink->Prepare()) {
            avformat_close_input(&fmt_ctx);
            return false;
        }

        audio_sink_ = static_cast<graph::AudioSinkNode*>(
            graph_->AddNode(std::move(asink)));

        // Connect: DemuxNode[audio_port] → DecoderNode → AudioSinkNode
        // Packet link: large capacity to avoid blocking DemuxNode
        // Frame link: moderate capacity for smooth audio output
        graph_->Connect(demux_outputs[audio_port_idx],
                        audio_decoder_->Inputs()[0], 256);
        graph_->Connect(audio_decoder_->Outputs()[0],
                        audio_sink_->Inputs()[0], 64);
        sink_count_++;
    }

    avformat_close_input(&fmt_ctx);

    // Run graph lifecycle to populate topo_order and advance state to kReady.
    // Individual nodes are already prepared (idempotent Prepare).
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
