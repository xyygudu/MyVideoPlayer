#include "mvp/transcoder.h"

#include <algorithm>
#include <utility>

#include <spdlog/spdlog.h>

#include "graph/media_graph.h"
#include "mvp/source_info.h"
#include "mvp/source_probe.h"
#include "nodes/decoder_node.h"
#include "nodes/demux_node.h"
#include "nodes/encoder_node.h"
#include "nodes/mux_node.h"

namespace mvp {

class Transcoder::Impl {
  public:
    void SetInput(const std::string& url) { input_url_ = url; }
    void SetOutput(const std::string& path, const TranscodeOptions& options) {
        output_path_ = path;
        options_ = options;
    }

    bool Start();
    void Cancel();
    void SetProgressCallback(ProgressCallback cb) { progress_cb_ = std::move(cb); }
    void SetCompletionCallback(CompletionCallback cb) { completion_cb_ = std::move(cb); }

  private:
    bool BuildGraph();
    void WireBranch(graph::OutputPort* demux_output, const EncodeParams& params,
                    graph::MuxNode* mux, int mux_port_index);
    void OnGraphEvent(graph::GraphEvent event);
    void OnProgress(double pts_seconds);
    void ReportCompletionOnce(bool ok);

    std::string input_url_;
    std::string output_path_;
    TranscodeOptions options_;
    SourceInfo info_;

    std::unique_ptr<graph::MediaGraph> graph_;
    bool completed_{false};

    ProgressCallback progress_cb_;
    CompletionCallback completion_cb_;
};

bool Transcoder::Impl::Start() {
    completed_ = false;
    if (!BuildGraph()) {
        ReportCompletionOnce(false);
        return false;
    }
    if (!graph_->Start()) {
        SPDLOG_ERROR("Transcoder: graph Start failed");
        ReportCompletionOnce(false);
        return false;
    }
    return true;
}

void Transcoder::Impl::Cancel() {
    if (graph_) {
        graph_->Stop();
    }
    ReportCompletionOnce(false);
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
    graph_->SetEventCallback([this](graph::GraphEvent e) { OnGraphEvent(e); });

    auto* demux = static_cast<graph::DemuxNode*>(graph_->AddNode(
        std::make_unique<graph::DemuxNode>(input_url_, video_idx, audio_idx)));
    auto* mux = static_cast<graph::MuxNode*>(graph_->AddNode(
        std::make_unique<graph::MuxNode>(output_path_, want_video, want_audio)));
    mux->SetProgressHook([this](double pts) { OnProgress(pts); });

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

    graph_->Connect(demux_output, dec->Inputs()[0], {15 * 1024 * 1024, 256});
    graph_->Connect(dec->Outputs()[0], enc->Inputs()[0]);
    graph_->Connect(enc->Outputs()[0], mux->Inputs()[mux_port_index]);
}

void Transcoder::Impl::OnGraphEvent(graph::GraphEvent event) {
    if (event == graph::GraphEvent::kEos) {
        ReportCompletionOnce(true);
    } else if (event == graph::GraphEvent::kError) {
        ReportCompletionOnce(false);
    }
}

void Transcoder::Impl::OnProgress(double pts_seconds) {
    if (!progress_cb_ || info_.duration <= 0.0) {
        return;
    }
    double pct = std::clamp((pts_seconds / info_.duration) * 100.0, 0.0, 100.0);
    progress_cb_(pct);
}

void Transcoder::Impl::ReportCompletionOnce(bool ok) {
    if (completed_) {
        return;
    }
    completed_ = true;
    if (completion_cb_) {
        completion_cb_(ok);
    }
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
