#ifndef MVP_NODES_DEMUX_NODE_H_
#define MVP_NODES_DEMUX_NODE_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "graph/media_buffer.h"
#include "graph/node.h"
#include "graph/port.h"

struct AVFormatContext;
struct AVPacket;

namespace mvp::graph {

/// Per-pipeline demux behaviour.
struct DemuxOptions {
    /// Fold timestamp jumps of AVFMT_TS_DISCONT inputs (MPEG-TS) into one
    /// continuous timeline, like ffmpeg's dts_delta_threshold handling.
    /// Transcoding needs it (muxers reject non-monotonic DTS); playback keeps
    /// raw timestamps like ffplay, so seek targets stay meaningful.
    bool correct_ts_discontinuities{false};
};

/// Source node: opens a media file and routes compressed packets to
/// per-stream output ports.
///
/// - NodeType: kSource (no input ports)
/// - ThreadingMode: kActive (owns demux thread)
/// - Construction: `DemuxNode(file_path, video_idx, audio_idx)` —
///   stream indices come from a prior SourceProbe::Probe() call.
///   -1 means no stream of that type. Output ports are created immediately.
/// - Open: opens the file; Negotiate then only reads codecpar
/// - Start: launches DemuxLoop thread
/// - Seek: OnCommand(kSeek) records the target; the worker thread executes
///   avformat_seek_file
/// - Timeline: packet timestamps are rebased so the container start time is
///   0 (mpv's rebase-start-time), so positions, seeks and the clock agree for
///   MPEG-TS and other sources that do not start at zero
/// - Errors: transient read errors are retried; a persistent one is posted
///   to the graph bus as kError instead of masquerading as end-of-stream
class DemuxNode : public INode {
  public:
    explicit DemuxNode(std::string file_path, int video_stream_idx = -1,
                       int audio_stream_idx = -1, DemuxOptions options = {});
    ~DemuxNode() override;

    // --- INode interface ---
    bool Open() override;
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override;
    void OnCommand(const Command& cmd) override;

    void Process(MediaBuffer /*input*/, OutputCallback /*emit*/) override {
        // Source node: no-op (never called)
    }

    std::vector<InputPort*> Inputs() override { return {}; }
    std::vector<OutputPort*> Outputs() override;

    NodeType Type() const override { return NodeType::kSource; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return "DemuxNode"; }

    /// Set graph reference for latching the seek epoch.
    void Attach(MediaGraph* graph) override { graph_ = graph; }

    // --- DemuxNode-specific ---

    /// Request a seek to the given position in seconds.
    /// Thread-safe. Actual seek performed in worker thread.
    void RequestSeek(double position_seconds);

    double Duration() const;
    int AudioStreamIndex() const { return audio_stream_index_; }
    int VideoStreamIndex() const { return video_stream_index_; }

  private:
    void DemuxLoop();
    void CloseFormatContext();

    
    bool OpenFile();
    void FindStreams();
    MediaFormat MakeStreamFormat(int stream_index, MediaType type,
                                 Rational frame_rate) const;

    // DemuxLoop helpers
    std::pair<OutputPort*, OutputPort*> StreamPorts() const;
    bool HandlePendingSeek();
    bool HasPendingSeek() const;
    void HandleReadFailure(int ret, int& read_errors, OutputPort* video_port,
                           OutputPort* audio_port);
    /// Parks the worker until a seek or Stop arrives, or `timeout` elapses.
    void WaitForWakeup(std::optional<std::chrono::milliseconds> timeout);
    void EmitEos(OutputPort* video_port, OutputPort* audio_port);
    /// Returns false once the downstream link is aborted (graph stopping).
    bool RoutePacket(AVPacketPtr pkt, OutputPort* video_port,
                     OutputPort* audio_port);
    void RefreshLocalSerial();
    void PostError(const std::string& detail);

    // Discontinuity correction state of one stream (demux thread only).
    static constexpr int64_t kNoDts = std::numeric_limits<int64_t>::min();
    struct DtsTrack {
        int64_t next_us{kNoDts};  // Predicted DTS of the next packet
        int64_t last_us{kNoDts};
    };
    void CorrectDiscontinuity(AVPacket* pkt, int stream_index, DtsTrack& track);

    /// AVIOInterruptCB: makes blocking FFmpeg IO return once Stop() is called.
    static int InterruptCallback(void* opaque);

    NodeState state_{NodeState::kIdle};
    std::string file_path_;
    const DemuxOptions options_;
    MediaGraph* graph_{nullptr};

    // FFmpeg state (owned, allocated in Prepare, freed in Stop)
    AVFormatContext* format_ctx_{nullptr};
    int audio_stream_index_{-1};
    int video_stream_index_{-1};

    // Output ports (one per stream, created in Prepare)
    std::vector<std::unique_ptr<OutputPort>> output_ports_;

    // Worker thread
    std::thread demux_thread_;
    // Control thread writes it (under wake_mutex_ when stopping), worker reads.
    std::atomic<bool> running_{false};
    // Set by Stop() so network reads blocked inside FFmpeg return AVERROR_EXIT.
    std::atomic<bool> abort_io_{false};

    // Parks the worker after EOF or a fatal read error until a seek or Stop.
    // RequestSeek/Stop publish their flag under the mutex, so the wakeup
    // cannot slip between the worker's predicate check and its block.
    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;

    // Pending seek request; -1 means none. exchange() makes read+clear
    // atomic, avoiding lost-update races between rapid seeks.
    static constexpr double kNoSeekPending = -1.0;
    std::atomic<double> pending_seek_{kNoSeekPending};

    // Container start time (AV_TIME_BASE units) subtracted from every packet.
    // Written by Open() before the worker starts; read-only afterwards.
    int64_t start_offset_us_{0};

    // Seek epoch latched from the graph after each reposition. Worker only.
    int local_serial_{0};

    // Discontinuity correction (worker only): offset accumulated so far.
    int64_t discont_offset_us_{0};
    DtsTrack video_dts_;
    DtsTrack audio_dts_;
};

}  // namespace mvp::graph

#endif  // MVP_NODES_DEMUX_NODE_H_
