#ifndef MVP_PLAYER_MEDIA_PLAYER_IMPL_H_
#define MVP_PLAYER_MEDIA_PLAYER_IMPL_H_

#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "graph/media_graph.h"
#include "graph/message_bus.h"
#include "mvp/media_player.h"
#include "mvp/source_info.h"
#include "player/effect_manager.h"
#include "video/video_renderer.h"

namespace mvp {

namespace graph {
class DecoderNode;
class OutputPort;
class VideoSinkNode;
}  // namespace graph

/// Threading: public methods may be called from any thread (normally the UI
/// thread); mutex_ serializes them with the internal event thread, which
/// drains the graph's message bus and is the only other writer of the
/// playback state. The finished callback runs on that thread, outside the
/// lock. The graph is replaced only by the event thread itself (device-lost
/// recovery) or by a caller that joined it first (Open, Close, SetOptions),
/// so the bus the event thread waits on always outlives the wait.
class MediaPlayer::Impl {
  public:
    Impl() = default;
    ~Impl() { Close(); }

    // --- media_player.cc: lifecycle, control, queries ---
    bool Open(const std::string& filepath);
    void Close();
    void Play();
    void Pause();
    void Seek(double position_seconds);
    PlaybackState State() const;
    double Duration() const;
    double CurrentPosition() const;
    double VideoFps() const;
    void SetWindowHandle(void* handle);
    void NotifyWindowResized(int w, int h);
    void SetFinishedCallback(MediaPlayer::FinishedCallback cb);
    void SetOptions(const PlayerOptions& options);
    PlayerOptions Options() const;
    PipelineInfo GetPipelineInfo() const;
    CapturedFrame CaptureFrame() const;
    std::vector<EffectInfo> EffectInfos() const;
    void SetEffectParam(const std::string& effect_id, const std::string& param_id,
                        EffectParamValue value);
    void SetEffectEnabled(const std::string& effect_id, bool enabled);
    void ResetEffectParams(const std::string& effect_id);

  private:
    /// Close() minus the public contract: false when called on the event
    /// thread (from the finished callback), where joining it would deadlock.
    bool CloseInternal();
    bool OpenLocked(const std::string& filepath);
    void PlayLocked();
    void SeekLocked(double position_seconds);

    // --- media_player_pipeline.cc: building and replacing the graph ---
    bool BuildGraph();
    void WireVideoBranch(graph::OutputPort* demux_output);
    void WireAudioBranch(graph::OutputPort* demux_output);
    graph::OutputPort* WireVideoEffects(graph::DecoderNode* vdec);
    void TeardownGraphLocked();
    /// Rebuilds the graph at the current position, keeping play/pause state
    /// and effect settings (options change, GPU device loss).
    bool RebuildLocked(bool recover_device);
    double ResumePositionLocked() const;
    void RestoreEffectsLocked(const std::vector<EffectInfo>& effects);
    void ResumeStateLocked(PlaybackState previous, bool was_started);
    /// Shows effect changes while paused by decoding the frame on screen again.
    void RefreshLocked();
    /// The current epoch reached the screen (or ended): run a deferred refresh.
    void SettleEpochLocked(int serial);

    // --- media_player_pipeline.cc: event thread ---
    void StartEventThread();
    bool StopEventThread();
    void EventLoop();
    void HandleMessage(const graph::GraphMessage& msg);
    /// True when this EOS completes playback (every sink reported it).
    bool HandleEosLocked(const graph::GraphMessage& msg);
    void HandleErrorLocked(const graph::GraphMessage& msg);
    void HandleDeviceLostLocked();

    // Guards every member below except event_thread_, which only the
    // Open/Close/SetOptions caller touches.
    mutable std::mutex mutex_;
    std::thread event_thread_;

    PlayerOptions options_;

    // Source meta (populated by SourceProbe in Open, consumed by BuildGraph)
    SourceInfo info_;

    // Selected stream indices (default: first of each type, -1 = none)
    int video_stream_index_{-1};
    int audio_stream_index_{-1};
    int sink_count_{0};  // Sinks that must report EOS before playback is finished
    // Sinks that reported EOS in seek epoch eos_epoch_.
    std::set<const graph::INode*> eos_sinks_;
    int eos_epoch_{-1};

    // Latest epoch whose first frame (or EOS) reached the video sink; a
    // refresh requested before then is folded into one follow-up.
    int settled_epoch_{-1};
    bool refresh_again_{false};
    bool started_{false};  // graph_ was started (left kReady)

    // Device-lost recoveries in the current window, to stop a rebuild loop.
    int recoveries_{0};
    std::chrono::steady_clock::time_point recovery_window_start_{};

    // Graph (owns all nodes) and non-owning views into it.
    std::unique_ptr<graph::MediaGraph> graph_;
    graph::VideoSinkNode* video_sink_{nullptr};
    graph::DecoderNode* video_decoder_{nullptr};

    // Indexes the effect nodes WireVideoEffects wires into the graph.
    // Non-owning — cleared before graph_ is reset (TeardownGraphLocked).
    EffectManager effect_manager_;

    // Video rendering
    VideoRenderer video_renderer_;
    void* window_handle_{nullptr};
    int window_width_{640};
    int window_height_{480};

    PlaybackState state_{PlaybackState::kIdle};
    MediaPlayer::FinishedCallback finished_cb_;
};

}  // namespace mvp

#endif  // MVP_PLAYER_MEDIA_PLAYER_IMPL_H_
