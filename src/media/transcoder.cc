#include "mvp/transcoder.h"

#include <algorithm>
#include <mutex>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

#include "graph/media_graph.h"
#include "graph/message_bus.h"
#include "mvp/source_info.h"
#include "mvp/source_probe.h"
#include "nodes/decoder_node.h"
#include "nodes/demux_node.h"
#include "nodes/encoder_node.h"
#include "nodes/mux_node.h"

namespace mvp {
namespace {
// Transcoding has no A/V sync look-ahead, so this only needs to keep the
// encoder fed. Encoding a frame costs orders of magnitude more than decoding
// one, so a single frame would already prevent starvation; 4 leaves room for
// I-frame decode spikes. Going deeper buys nothing — libx264 already holds
// rc_lookahead (40 by default) frames of its own.
constexpr int kTranscodeFrameDepth = 4;

const char* ResultName(TranscodeResult result) {
    switch (result) {
        case TranscodeResult::kSucceeded:
            return "succeeded";
        case TranscodeResult::kCancelled:
            return "cancelled";
        default:
            return "failed";
    }
}
}  // namespace

/// Threading: the owner thread runs Start/Cancel/destruction; an event thread
/// per run drains the graph bus. Whoever claims the completion first (the
/// event thread on EOS/error, or Cancel) stops the graph and reports, so the
/// graph is stopped and the callback invoked exactly once per run.
class Transcoder::Impl {
  public:
    ~Impl();

    void SetInput(const std::string& url) { input_url_ = url; }
    void SetOutput(const std::string& path, const TranscodeOptions& options) {
        output_path_ = path;
        options_ = options;
    }

    bool Start();
    void Cancel() { Complete(TranscodeResult::kCancelled); }
    void SetProgressCallback(ProgressCallback cb) { progress_cb_ = std::move(cb); }
    void SetCompletionCallback(CompletionCallback cb) { completion_cb_ = std::move(cb); }

  private:
    bool BuildGraph();
    void WireBranch(graph::OutputPort* demux_output, const EncodeParams& params,
                    graph::MuxNode* mux, int mux_port_index);
    void OnProgress(double pts_seconds);
    void StartEventThread();
    void StopEventThread();
    /// Ends the run once: the first caller stops the graph and reports.
    void Complete(TranscodeResult result);

    std::string input_url_;
    std::string output_path_;
    TranscodeOptions options_;
    SourceInfo info_;

    std::unique_ptr<graph::MediaGraph> graph_;
    const graph::INode* mux_{nullptr};  // Its EOS means the output is complete
    std::thread event_thread_;          // Drains graph_->Bus() during a run

    std::mutex mutex_;
    bool completed_{true};  // Guarded by mutex_; true while no run is active

    ProgressCallback progress_cb_;
    CompletionCallback completion_cb_;
};

Transcoder::Impl::~Impl() {
    {
        std::lock_guard lock(mutex_);
        completed_ = true;  // Destruction reports nothing (unless already in flight).
    }
    StopEventThread();
    if (graph_) {
        graph_->Stop();  // Before the callbacks the node threads may still call.
    }
}

bool Transcoder::Impl::Start() {
    {
        std::lock_guard lock(mutex_);
        if (!completed_) {
            SPDLOG_ERROR("Transcoder: Start() while a transcode is running, ignored");
            return false;
        }
    }
    StopEventThread();  // The previous run's thread, already done.
    graph_.reset();
    {
        std::lock_guard lock(mutex_);
        completed_ = false;
    }
    if (!BuildGraph() || !graph_->Start()) {
        SPDLOG_ERROR("Transcoder: failed to start '{}' -> '{}'", input_url_, output_path_);
        Complete(TranscodeResult::kFailed);
        return false;
    }
    StartEventThread();
    return true;
}

void Transcoder::Impl::StartEventThread() {
    graph::MessageBus* bus = &graph_->Bus();
    event_thread_ = std::thread([this, bus] {
        while (auto msg = bus->Wait()) {
            if (msg->type == graph::GraphMessageType::kError) {
                SPDLOG_ERROR("Transcoder: {} reported: {}",
                             msg->source ? msg->source->Name() : std::string("graph"), msg->detail);
                Complete(TranscodeResult::kFailed);
                return;  // *this may be gone: the callback can destroy it.
            }
            if (msg->source == mux_) {
                Complete(TranscodeResult::kSucceeded);
                return;
            }
        }
    });
}

void Transcoder::Impl::StopEventThread() {
    if (!event_thread_.joinable()) {
        return;
    }
    if (event_thread_.get_id() == std::this_thread::get_id()) {
        // From inside the completion callback: the thread returns right after
        // it without touching *this again, so it can simply be released.
        event_thread_.detach();
        return;
    }
    if (graph_) {
        graph_->Bus().Shutdown();
    }
    event_thread_.join();
}

void Transcoder::Impl::Complete(TranscodeResult result) {
    {
        std::lock_guard lock(mutex_);
        if (completed_) {
            return;
        }
        completed_ = true;
    }
    // Only the claimer gets here. Stop joins every node thread, including a
    // muxer thread that may be inside the progress callback.
    if (graph_) {
        graph_->Stop();
    }
    SPDLOG_INFO("Transcoder: '{}' -> '{}' {}", input_url_, output_path_, ResultName(result));
    // A local copy: the callback may destroy *this (and completion_cb_).
    CompletionCallback notify = completion_cb_;
    if (notify) {
        notify(result);
    }
}

bool Transcoder::Impl::BuildGraph() {
    info_ = SourceProbe::Probe(input_url_);
    if (info_.video_streams.empty() && info_.audio_streams.empty()) {
        SPDLOG_ERROR("Transcoder: source probe found no streams for '{}'", input_url_);
        return false;
    }

    bool want_video = !options_.video.codec_name.empty() && !info_.video_streams.empty();
    bool want_audio = !options_.audio.codec_name.empty() && !info_.audio_streams.empty();
    int video_idx = want_video ? info_.video_streams[0].index : -1;
    int audio_idx = want_audio ? info_.audio_streams[0].index : -1;

    graph_ = std::make_unique<graph::MediaGraph>();

    // Like the ffmpeg CLI: fold timestamp jumps so the muxer sees monotonic DTS.
    graph::DemuxOptions demux_options;
    demux_options.correct_ts_discontinuities = true;
    auto* demux = static_cast<graph::DemuxNode*>(graph_->AddNode(std::make_unique<graph::DemuxNode>(
        input_url_, video_idx, audio_idx, demux_options)));
    auto* mux = static_cast<graph::MuxNode*>(graph_->AddNode(
        std::make_unique<graph::MuxNode>(output_path_, want_video, want_audio)));
    mux->SetProgressHook([this](double pts) { OnProgress(pts); });
    mux_ = mux;

    if (want_video) {
        WireBranch(demux->Outputs()[0], options_.video, mux, 0);
    }
    if (want_audio) {
        int demux_port = want_video ? 1 : 0;
        int mux_port = want_video ? 1 : 0;
        WireBranch(demux->Outputs()[demux_port], options_.audio, mux, mux_port);
    }

    if (!graph_->Open()) {
        SPDLOG_ERROR("Transcoder: graph Open failed");
        return false;
    }
    if (!graph_->Negotiate()) {
        SPDLOG_ERROR("Transcoder: graph Negotiate failed");
        return false;
    }
    if (!graph_->Prepare()) {
        SPDLOG_ERROR("Transcoder: graph Prepare failed");
        return false;
    }
    return true;
}

void Transcoder::Impl::WireBranch(graph::OutputPort* demux_output,
                                  const EncodeParams& params, graph::MuxNode* mux,
                                  int mux_port_index) {
    auto* dec = static_cast<graph::DecoderNode*>(
        graph_->AddNode(std::make_unique<graph::DecoderNode>()));
    auto* enc = static_cast<graph::EncoderNode*>(
        graph_->AddNode(std::make_unique<graph::EncoderNode>(params)));

    graph_->Connect(demux_output, dec->Inputs()[0],
                    graph::LinkCapacity::ForPackets());
    graph_->Connect(dec->Outputs()[0], enc->Inputs()[0],
                    graph::LinkCapacity::ForFrames(kTranscodeFrameDepth));
    graph_->Connect(enc->Outputs()[0], mux->Inputs()[mux_port_index],
                    graph::LinkCapacity::ForPackets());
}

void Transcoder::Impl::OnProgress(double pts_seconds) {
    if (!progress_cb_ || info_.duration <= 0.0) {
        return;
    }
    double pct = std::clamp((pts_seconds / info_.duration) * 100.0, 0.0, 100.0);
    progress_cb_(pct);
}

// --- Transcoder public interface (delegates to Impl) ---

Transcoder::Transcoder() : impl_(std::make_unique<Impl>()) {}
Transcoder::~Transcoder() = default;

void Transcoder::SetInput(const std::string& url) { impl_->SetInput(url); }
void Transcoder::SetOutput(const std::string& path, const TranscodeOptions& options) {
    impl_->SetOutput(path, options);
}
bool Transcoder::Start() { return impl_->Start(); }
void Transcoder::Cancel() { impl_->Cancel(); }
void Transcoder::SetProgressCallback(ProgressCallback cb) {
    impl_->SetProgressCallback(std::move(cb));
}
void Transcoder::SetCompletionCallback(CompletionCallback cb) {
    impl_->SetCompletionCallback(std::move(cb));
}

}  // namespace mvp
