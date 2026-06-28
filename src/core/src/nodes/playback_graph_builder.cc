#include "nodes/playback_graph_builder.h"

#include "graph/media_graph.h"
#include "graph/port.h"
#include "nodes/audio_sink_node.h"
#include "nodes/decoder_node.h"
#include "video_renderer.h"

namespace mvp::graph {

void PlaybackGraphBuilder::AddVideoPipeline(
    const StreamInfo& stream, OutputPort* source,
    const std::vector<FilterSpec>& filters) {
    std::vector<INode*> chain;

    // Decoder
    auto decoder = std::make_unique<DecoderNode>();
    decoder->SetGraph(ctx_.graph);
    chain.push_back(ctx_.graph->AddNode(std::move(decoder)));

    // Filters (none for plain playback; reserved for the filter-chain feature)
    (void)filters;

    // Video sink
    const auto& enc = stream.format.AsEncoded();
    double fps = (enc.frame_rate.den > 0)
                     ? static_cast<double>(enc.frame_rate.num) /
                           enc.frame_rate.den
                     : 30.0;

    auto sink = std::make_unique<VideoSinkNode>();
    sink->SetRenderer(ctx_.renderer);
    sink->SetAudioClock(ctx_.audio_clock);
    sink->SetVideoClock(ctx_.video_clock);
    sink->SetVideoFps(fps);
    sink->SetGraph(ctx_.graph);
    if (ctx_.video_cb) sink->SetFrameCallback(ctx_.video_cb);
    sink->SetSyncMode(ctx_.has_audio
                          ? VideoSinkNode::SyncMode::kAudioMaster
                          : VideoSinkNode::SyncMode::kVideoMaster);

    // Open the renderer with the stream resolution.
    int w = enc.codec_params ? enc.codec_params->width : 0;
    int h = enc.codec_params ? enc.codec_params->height : 0;
    if (ctx_.window_handle && w > 0 && h > 0) {
        ctx_.renderer->Open(ctx_.window_handle, w, h);
    }

    chain.push_back(ctx_.graph->AddNode(std::move(sink)));

    // Packet link 256 (source -> decoder), frame link 8 (decoder -> sink).
    ConnectChain(source, chain, 256, 8);
}

void PlaybackGraphBuilder::AddAudioPipeline(
    const StreamInfo& stream, OutputPort* source,
    const std::vector<FilterSpec>& filters) {
    (void)stream;
    std::vector<INode*> chain;

    // Decoder
    auto decoder = std::make_unique<DecoderNode>();
    decoder->SetGraph(ctx_.graph);
    chain.push_back(ctx_.graph->AddNode(std::move(decoder)));

    // Filters (reserved)
    (void)filters;

    // Audio sink (reads sample_rate/channels from negotiated format in Prepare)
    auto sink = std::make_unique<AudioSinkNode>();
    sink->SetAudioClock(ctx_.audio_clock);
    sink->SetGraph(ctx_.graph);
    chain.push_back(ctx_.graph->AddNode(std::move(sink)));

    // Packet link 256 (source -> decoder), frame link 64 (decoder -> sink).
    ConnectChain(source, chain, 256, 64);
}

void PlaybackGraphBuilder::ConnectChain(OutputPort* source,
                                        const std::vector<INode*>& chain,
                                        int first_capacity,
                                        int rest_capacity) {
    if (chain.empty()) {
        return;
    }
    ctx_.graph->Connect(source, chain.front()->Inputs()[0], first_capacity);
    for (size_t i = 0; i + 1 < chain.size(); ++i) {
        ctx_.graph->Connect(chain[i]->Outputs()[0], chain[i + 1]->Inputs()[0],
                            rest_capacity);
    }
}

}  // namespace mvp::graph
