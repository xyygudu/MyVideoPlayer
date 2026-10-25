#include "mvp/recorder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

#include "graph/media_graph.h"
#include "graph/message_bus.h"
#include "nodes/audio_capture_node.h"
#include "nodes/audio_mixer_node.h"
#include "nodes/camera_capture_node.h"
#include "nodes/encoder_node.h"
#include "nodes/mux_node.h"
#include "nodes/screen_capture_node.h"
#include "nodes/video_compositor_node.h"

namespace mvp {
namespace {

// Link depths. Raw screen frames are big (8 MB at 1080p), so only a few
// absorb encoder hiccups; camera frames are drained newest-first; audio
// packets (10 ms) and mix chunks (21 ms) are small.
constexpr int kVideoDepth = 6;
constexpr int kCameraDepth = 4;
constexpr int kAudioDepth = 64;
constexpr int kMixDepth = 16;

constexpr auto kEventPoll = std::chrono::milliseconds(250);
// Draining encoders and finalizing the file takes well under a second.
constexpr auto kStopTimeout = std::chrono::seconds(15);

// Real-time speed presets, OBS's defaults for these encoders. Empty keeps
// the encoder's default (AMF's is already real-time).
std::string RealtimePreset(const std::string& codec) {
    if (codec == "libx264") return "veryfast";
    if (codec.find("_nvenc") != std::string::npos) return "p4";
    if (codec.find("_qsv") != std::string::npos) return "veryfast";
    return "";
}

EncodeParams RealtimeVideoParams(EncodeParams params, int frame_rate) {
    params.rate_control = RateControlMode::kCrf;
    params.preset = RealtimePreset(params.codec_name);
    params.gop_size = std::max(1, frame_rate) * 2;  // A keyframe every 2 s keeps the file seekable
    // B-frame support differs between hardware encoder generations.
    params.max_b_frames = params.codec_name == "libx264" ? 2 : 0;
    return params;
}

const char* ResultName(RecordResult result) {
    return result == RecordResult::kSucceeded ? "succeeded" : "failed";
}

}  // namespace

/// Threading: the owner thread runs Start/Pause/Resume/Stop and destruction;
/// an event thread per recording drains the graph bus. Whoever claims the
/// completion first (the event thread on the muxer's EOS, an error or the
/// stop timeout) stops the graph and reports, exactly once per recording.
/// mutex_ also serializes graph control between the two threads.
class Recorder::Impl {
  public:
    ~Impl();

    bool Start(const RecordOptions& options);
    void Pause() { SetPaused(true); }
    void Resume() { SetPaused(false); }
    void Stop();
    RecorderState State() const { return state_.load(); }
    RecordingStats Stats() const;
    RecordingSources Sources() const { return sources_; }
    void SetVolume(AudioSourceKind source, float volume);
    void SetCompletionCallback(CompletionCallback cb) { completion_cb_ = std::move(cb); }

  private:
    // Nodes the facade talks to; owned by graph_.
    struct Nodes {
        graph::ScreenCaptureNode* screen{nullptr};
        graph::CameraCaptureNode* camera{nullptr};
        graph::AudioCaptureNode* system_audio{nullptr};
        graph::AudioCaptureNode* microphone{nullptr};
        graph::AudioMixerNode* mixer{nullptr};
        graph::MuxNode* mux{nullptr};
        int system_input{-1};  // Mixer inputs
        int microphone_input{-1};
    };

    template <typename T, typename... Args>
    T* Add(Args&&... args) {
        return static_cast<T*>(graph_->AddNode(std::make_unique<T>(std::forward<Args>(args)...)));
    }

    /// Builds and opens the graph; optional sources that fail are dropped.
    bool OpenGraph(RecordOptions options);
    bool DropFailedOptionalSource(RecordOptions* options) const;
    void BuildGraph(const RecordOptions& options);
    graph::OutputPort* AddVideoSource(const RecordOptions& options);
    graph::OutputPort* AddAudioMix(const RecordOptions& options);
    graph::AudioCaptureNode* AddAudioSource(AudioDeviceKind kind, const AudioCaptureOptions& options,
                                            int mixer_input);
    void AddEncoder(graph::OutputPort* from, EncodeParams params, int depth, graph::InputPort* to);

    void SetPaused(bool paused);
    void FreezeElapsed();  // Caller holds mutex_
    void StartEventThread();
    void StopEventThread();
    bool StopTimedOut();
    /// Ends the recording once: the first caller stops the graph and reports.
    void Complete(RecordResult result);

    std::unique_ptr<graph::MediaGraph> graph_;
    Nodes nodes_;
    RecordOptions options_;  // As recorded, after dropping failed sources
    RecordingSources sources_;
    std::atomic<RecorderState> state_{RecorderState::kIdle};
    std::atomic<double> final_elapsed_{-1.0};  // Running time at stop; < 0 while recording
    std::thread event_thread_;

    std::mutex mutex_;
    bool completed_{true};  // Guarded by mutex_; true while not recording
    std::chrono::steady_clock::time_point stop_deadline_;  // Guarded by mutex_

    CompletionCallback completion_cb_;
};

Recorder::Impl::~Impl() {
    {
        std::lock_guard lock(mutex_);
        completed_ = true;  // Destruction reports nothing (unless already in flight).
    }
    StopEventThread();
    if (graph_) {
        graph_->Stop();  // The muxer still writes a trailer: the file stays playable.
    }
}

bool Recorder::Impl::Start(const RecordOptions& options) {
    {
        std::lock_guard lock(mutex_);
        if (!completed_) {
            SPDLOG_ERROR("Recorder: Start() while recording, ignored");
            return false;
        }
    }
    StopEventThread();  // The previous recording's thread, already done.
    graph_.reset();
    if (!OpenGraph(options) || !graph_->Start()) {
        SPDLOG_ERROR("Recorder: cannot start recording to '{}'", options.output_path);
        graph_.reset();
        return false;
    }
    final_elapsed_ = -1.0;
    {
        std::lock_guard lock(mutex_);
        completed_ = false;
    }
    state_ = RecorderState::kRecording;
    StartEventThread();
    SPDLOG_INFO("Recorder: recording to '{}' (system audio {}, microphone {}, camera {})", options_.output_path,
                sources_.system_audio, sources_.microphone, sources_.camera);
    return true;
}

bool Recorder::Impl::OpenGraph(RecordOptions options) {
    for (;;) {
        BuildGraph(options);
        if (graph_->Open()) {
            break;
        }
        if (!DropFailedOptionalSource(&options)) {
            return false;
        }
    }
    if (!graph_->Negotiate() || !graph_->Prepare()) {
        graph_->Stop();  // Joins the device threads the sources opened
        return false;
    }
    options_ = std::move(options);
    sources_ = {nodes_.system_audio != nullptr, nodes_.microphone != nullptr, nodes_.camera != nullptr};
    return true;
}

bool Recorder::Impl::DropFailedOptionalSource(RecordOptions* options) const {
    auto failed = [](const graph::INode* node) { return node && node->State() == graph::NodeState::kError; };
    if (failed(nodes_.camera)) {
        SPDLOG_WARN("Recorder: the camera cannot be opened, recording without it");
        options->camera.enabled = false;
    } else if (failed(nodes_.microphone)) {
        SPDLOG_WARN("Recorder: the microphone cannot be opened, recording without it");
        options->microphone.enabled = false;
    } else if (failed(nodes_.system_audio)) {
        SPDLOG_WARN("Recorder: the system audio cannot be captured, recording without it");
        options->system_audio.enabled = false;
    } else {
        return false;  // The screen or the output: no recording without them
    }
    return true;
}

void Recorder::Impl::BuildGraph(const RecordOptions& options) {
    graph_ = std::make_unique<graph::MediaGraph>();
    nodes_ = {};
    const bool has_audio = options.system_audio.enabled || options.microphone.enabled;
    nodes_.mux = Add<graph::MuxNode>(options.output_path, true, has_audio);

    graph::OutputPort* video = AddVideoSource(options);
    AddEncoder(video, RealtimeVideoParams(options.video_encoder, options.video.frame_rate), kVideoDepth,
               nodes_.mux->Inputs()[0]);
    if (has_audio) {
        AddEncoder(AddAudioMix(options), options.audio_encoder, kMixDepth, nodes_.mux->Inputs()[1]);
    }
}

graph::OutputPort* Recorder::Impl::AddVideoSource(const RecordOptions& options) {
    nodes_.screen = Add<graph::ScreenCaptureNode>(options.video);
    graph::OutputPort* screen = nodes_.screen->Outputs()[0];
    if (!options.camera.enabled) {
        return screen;
    }
    nodes_.camera = Add<graph::CameraCaptureNode>(options.camera.device_id);
    auto* compositor = Add<graph::VideoCompositorNode>(options.camera);
    graph_->Connect(screen, compositor->Inputs()[0], graph::LinkCapacity::ForFrames(kVideoDepth));
    graph_->Connect(nodes_.camera->Outputs()[0], compositor->Inputs()[1],
                    graph::LinkCapacity::ForFrames(kCameraDepth));
    return compositor->Outputs()[0];
}

graph::OutputPort* Recorder::Impl::AddAudioMix(const RecordOptions& options) {
    const int inputs = (options.system_audio.enabled ? 1 : 0) + (options.microphone.enabled ? 1 : 0);
    nodes_.mixer = Add<graph::AudioMixerNode>(inputs);
    int next_input = 0;
    if (options.system_audio.enabled) {
        nodes_.system_input = next_input++;
        nodes_.system_audio = AddAudioSource(AudioDeviceKind::kOutput, options.system_audio, nodes_.system_input);
    }
    if (options.microphone.enabled) {
        nodes_.microphone_input = next_input++;
        nodes_.microphone = AddAudioSource(AudioDeviceKind::kInput, options.microphone, nodes_.microphone_input);
    }
    return nodes_.mixer->Outputs()[0];
}

graph::AudioCaptureNode* Recorder::Impl::AddAudioSource(AudioDeviceKind kind, const AudioCaptureOptions& options,
                                                        int mixer_input) {
    auto* source = Add<graph::AudioCaptureNode>(kind, options.device_id);
    graph_->Connect(source->Outputs()[0], nodes_.mixer->Inputs()[static_cast<size_t>(mixer_input)],
                    graph::LinkCapacity::ForFrames(kAudioDepth));
    nodes_.mixer->SetGain(mixer_input, options.volume);
    return source;
}

void Recorder::Impl::AddEncoder(graph::OutputPort* from, EncodeParams params, int depth, graph::InputPort* to) {
    auto* encoder = Add<graph::EncoderNode>(std::move(params));
    graph_->Connect(from, encoder->Inputs()[0], graph::LinkCapacity::ForFrames(depth));
    graph_->Connect(encoder->Outputs()[0], to, graph::LinkCapacity::ForPackets());
}

void Recorder::Impl::SetPaused(bool paused) {
    std::lock_guard lock(mutex_);
    const RecorderState from = paused ? RecorderState::kRecording : RecorderState::kPaused;
    if (completed_ || state_ != from) {
        return;
    }
    // Every clock stops, so the paused time is cut out of the recording.
    graph_->SetPaused(paused);
    state_ = paused ? RecorderState::kPaused : RecorderState::kRecording;
    SPDLOG_INFO("Recorder: {}", paused ? "paused" : "resumed");
}

void Recorder::Impl::Stop() {
    std::lock_guard lock(mutex_);
    if (completed_ || state_ == RecorderState::kStopping) {
        return;
    }
    FreezeElapsed();
    stop_deadline_ = std::chrono::steady_clock::now() + kStopTimeout;
    state_ = RecorderState::kStopping;
    // Sources end their streams; encoders drain; the muxer's EOS completes.
    graph_->SendCommand({graph::CommandType::kEndOfStream});
}

void Recorder::Impl::FreezeElapsed() {
    if (final_elapsed_.load() < 0.0) {
        const IClock* clock = graph_ ? graph_->MasterClock() : nullptr;
        final_elapsed_ = clock ? std::max(0.0, clock->Get()) : 0.0;
    }
}

RecordingStats Recorder::Impl::Stats() const {
    RecordingStats stats;
    if (!graph_) {
        return stats;
    }
    const double frozen = final_elapsed_.load();
    const IClock* clock = graph_->MasterClock();
    stats.elapsed_seconds = frozen >= 0.0 ? frozen : (clock ? std::max(0.0, clock->Get()) : 0.0);
    if (nodes_.screen) {
        stats.frames_captured = nodes_.screen->FramesCaptured();
        stats.frames_dropped = nodes_.screen->FramesDropped();
    }
    if (nodes_.mixer && nodes_.system_input >= 0) {
        stats.system_audio_level = nodes_.mixer->TakeLevel(nodes_.system_input);
    }
    if (nodes_.mixer && nodes_.microphone_input >= 0) {
        stats.microphone_level = nodes_.mixer->TakeLevel(nodes_.microphone_input);
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(std::filesystem::u8path(options_.output_path), error);
    stats.bytes_written = error ? 0 : static_cast<int64_t>(size);
    return stats;
}

void Recorder::Impl::SetVolume(AudioSourceKind source, float volume) {
    const int input = source == AudioSourceKind::kSystemAudio ? nodes_.system_input : nodes_.microphone_input;
    if (graph_ && nodes_.mixer && input >= 0) {
        nodes_.mixer->SetGain(input, volume);
    }
}

void Recorder::Impl::StartEventThread() {
    graph::MessageBus* bus = &graph_->Bus();
    const graph::INode* mux = nodes_.mux;
    event_thread_ = std::thread([this, bus, mux] {
        for (;;) {
            const std::optional<graph::GraphMessage> msg = bus->WaitFor(kEventPoll);
            if (!msg) {
                if (bus->IsShutDown()) {
                    return;
                }
                if (StopTimedOut()) {
                    SPDLOG_ERROR("Recorder: the recording did not finish in time, aborting it");
                    Complete(RecordResult::kFailed);
                    return;  // *this may be gone: the callback can destroy it.
                }
                continue;
            }
            if (msg->type == graph::GraphMessageType::kError) {
                SPDLOG_ERROR("Recorder: {} reported: {}", msg->source ? msg->source->Name() : "graph", msg->detail);
                Complete(RecordResult::kFailed);
                return;
            }
            if (msg->type == graph::GraphMessageType::kEos && msg->source == mux) {
                Complete(RecordResult::kSucceeded);
                return;
            }
        }
    });
}

void Recorder::Impl::StopEventThread() {
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

bool Recorder::Impl::StopTimedOut() {
    std::lock_guard lock(mutex_);
    return state_ == RecorderState::kStopping && std::chrono::steady_clock::now() > stop_deadline_;
}

void Recorder::Impl::Complete(RecordResult result) {
    {
        std::lock_guard lock(mutex_);
        if (completed_) {
            return;
        }
        completed_ = true;
        FreezeElapsed();
        // Joins every node thread. A muxer that has not finished (error,
        // timeout) still writes what it has, so the partial file plays.
        graph_->Stop();
    }
    state_ = RecorderState::kIdle;
    SPDLOG_INFO("Recorder: recording to '{}' {}", options_.output_path, ResultName(result));
    // A local copy: the callback may destroy *this (and completion_cb_).
    CompletionCallback notify = completion_cb_;
    if (notify) {
        notify(result);
    }
}

// --- Recorder public interface (delegates to Impl) ---

Recorder::Recorder() : impl_(std::make_unique<Impl>()) {}
Recorder::~Recorder() = default;

bool Recorder::Start(const RecordOptions& options) { return impl_->Start(options); }
void Recorder::Pause() { impl_->Pause(); }
void Recorder::Resume() { impl_->Resume(); }
void Recorder::Stop() { impl_->Stop(); }
RecorderState Recorder::State() const { return impl_->State(); }
RecordingStats Recorder::Stats() const { return impl_->Stats(); }
RecordingSources Recorder::Sources() const { return impl_->Sources(); }
void Recorder::SetVolume(AudioSourceKind source, float volume) { impl_->SetVolume(source, volume); }
void Recorder::SetCompletionCallback(CompletionCallback cb) { impl_->SetCompletionCallback(std::move(cb)); }

}  // namespace mvp
