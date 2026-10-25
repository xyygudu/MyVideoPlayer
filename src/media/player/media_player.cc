#include "mvp/media_player.h"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "mvp/source_probe.h"
#include "nodes/decoder_node.h"
#include "nodes/video_sink_node.h"
#include "player/media_player_impl.h"
#include "sync/clock.h"
#include "video/frame_capture.h"

namespace mvp {

// --- Impl: lifecycle ---

bool MediaPlayer::Impl::Open(const std::string& filepath) {
    if (!CloseInternal()) {
        return false;
    }
    std::lock_guard lock(mutex_);
    if (!OpenLocked(filepath)) {
        state_ = PlaybackState::kError;
        return false;
    }
    StartEventThread();
    state_ = PlaybackState::kReady;
    return true;
}

bool MediaPlayer::Impl::OpenLocked(const std::string& filepath) {
    // 1. Probe source independently of Graph construction.
    info_ = SourceProbe::Probe(filepath);
    if (info_.video_streams.empty() && info_.audio_streams.empty()) {
        SPDLOG_ERROR("MediaPlayer: source probe found no streams");
        return false;
    }

    // 2. Select default streams (first of each type).
    video_stream_index_ = info_.video_streams.empty() ? -1 : info_.video_streams[0].index;
    audio_stream_index_ = info_.audio_streams.empty() ? -1 : info_.audio_streams[0].index;
    sink_count_ = (video_stream_index_ >= 0 ? 1 : 0) + (audio_stream_index_ >= 0 ? 1 : 0);

    // 3. Open the renderer first: negotiation needs its backend device to
    // decide whether the chain can run hardware decode.
    video_renderer_.Open(window_handle_, window_width_, window_height_);

    // 4. Build the playback graph.
    return BuildGraph();
}

void MediaPlayer::Impl::Close() {
    CloseInternal();
}

bool MediaPlayer::Impl::CloseInternal() {
    if (!StopEventThread()) {
        return false;
    }
    std::lock_guard lock(mutex_);
    TeardownGraphLocked();
    SPDLOG_DEBUG("MediaPlayer: closing renderer");
    video_renderer_.Close();

    info_ = SourceInfo{};
    video_stream_index_ = -1;
    audio_stream_index_ = -1;
    sink_count_ = 0;

    state_ = PlaybackState::kIdle;
    return true;
}

void MediaPlayer::Impl::Play() {
    std::lock_guard lock(mutex_);
    PlayLocked();
}

void MediaPlayer::Impl::PlayLocked() {
    switch (state_) {
        case PlaybackState::kReady:
            if (!graph_->Start()) {
                SPDLOG_ERROR("MediaPlayer: graph Start failed");
                state_ = PlaybackState::kError;
                return;
            }
            started_ = true;
            state_ = PlaybackState::kPlaying;
            return;
        case PlaybackState::kPaused:
            SPDLOG_INFO("MediaPlayer: resume");
            graph_->SetPaused(false);
            state_ = PlaybackState::kPlaying;
            return;
        case PlaybackState::kFinished:
            // Replay from the beginning: the seek leaves the player paused.
            SeekLocked(0.0);
            PlayLocked();
            return;
        default:
            return;
    }
}

void MediaPlayer::Impl::Pause() {
    std::lock_guard lock(mutex_);
    if (state_ != PlaybackState::kPlaying) return;

    SPDLOG_INFO("MediaPlayer: pause");
    graph_->SetPaused(true);
    state_ = PlaybackState::kPaused;
}

void MediaPlayer::Impl::Seek(double position_seconds) {
    std::lock_guard lock(mutex_);
    SeekLocked(position_seconds);
}

void MediaPlayer::Impl::SeekLocked(double position_seconds) {
    if (!graph_ || state_ == PlaybackState::kIdle || state_ == PlaybackState::kError) return;

    position_seconds = std::max(0.0, position_seconds);
    SPDLOG_INFO("MediaPlayer: seek to {:.3f}s (state={})", position_seconds,
                static_cast<int>(state_));
    // Graph coordinates the seek: flush all links, reposition every clock in
    // a new epoch, then broadcast a seek command so each node resets its own
    // state (decoders drop to target, demux repositions). EOS reports of the
    // old epoch become stale.
    graph_->Seek(position_seconds);
    eos_sinks_.clear();
    refresh_again_ = false;  // The seek decodes through the current effect settings.
    if (state_ == PlaybackState::kFinished) {
        state_ = PlaybackState::kPaused;  // EOS paused the graph; stay paused.
    }
}

// --- Impl: queries ---

PlaybackState MediaPlayer::Impl::State() const {
    std::lock_guard lock(mutex_);
    return state_;
}

double MediaPlayer::Impl::Duration() const {
    std::lock_guard lock(mutex_);
    return info_.duration;
}

double MediaPlayer::Impl::CurrentPosition() const {
    std::lock_guard lock(mutex_);
    IClock* clock = graph_ ? graph_->MasterClock() : nullptr;
    return clock ? clock->Get() : 0.0;
}

double MediaPlayer::Impl::VideoFps() const {
    std::lock_guard lock(mutex_);
    if (info_.video_streams.empty()) return 30.0;
    const auto& vs = info_.video_streams[0];
    return (vs.frame_rate_den > 0) ? static_cast<double>(vs.frame_rate_num) / vs.frame_rate_den : 30.0;
}

PipelineInfo MediaPlayer::Impl::GetPipelineInfo() const {
    std::lock_guard lock(mutex_);
    PipelineInfo info;
    info.renderer = video_renderer_.BackendName();
    if (video_decoder_) {
        info.video_codec = video_decoder_->CodecName();
        info.hardware_decoding = video_decoder_->IsHardwareDecoding();
    }
    if (video_sink_) {
        info.frames_presented = video_sink_->FramesPresented();
    }
    return info;
}

CapturedFrame MediaPlayer::Impl::CaptureFrame() const {
    CapturedFrame captured;
    MediaFrame frame;
    {
        std::lock_guard lock(mutex_);
        if (video_sink_) {
            frame = video_sink_->DisplayedFrame(&captured.pts);
        }
    }
    // Our reference keeps the frame alive, so the slow conversion runs unlocked.
    if (!frame.IsValid() || !ConvertToRgba(frame, &captured.rgba)) {
        return CapturedFrame{};
    }
    captured.width = frame.width();
    captured.height = frame.height();
    return captured;
}

// --- Impl: window, options, callbacks ---

void MediaPlayer::Impl::SetWindowHandle(void* handle) {
    std::lock_guard lock(mutex_);
    window_handle_ = handle;
}

void MediaPlayer::Impl::NotifyWindowResized(int w, int h) {
    std::lock_guard lock(mutex_);
    // Recorded for the renderer's initial Open; the live update goes through
    // the graph so only the render thread touches the renderer.
    window_width_ = w;
    window_height_ = h;
    if (graph_) {
        graph_->SendCommand({graph::CommandType::kResize, 0.0, w, h});
    }
}

void MediaPlayer::Impl::SetOptions(const PlayerOptions& options) {
    if (!StopEventThread()) {
        return;
    }
    std::lock_guard lock(mutex_);
    options_ = options;
    if (!graph_) {
        return;  // Applied by the next Open().
    }
    SPDLOG_INFO("MediaPlayer: options changed (hw decode {}, gpu effects {}, zero-copy {})",
                options.hardware_decode, options.gpu_effects, options.zero_copy);
    RebuildLocked(/*recover_device=*/false);
    StartEventThread();
}

PlayerOptions MediaPlayer::Impl::Options() const {
    std::lock_guard lock(mutex_);
    return options_;
}

void MediaPlayer::Impl::SetFinishedCallback(MediaPlayer::FinishedCallback cb) {
    std::lock_guard lock(mutex_);
    finished_cb_ = std::move(cb);
}

// --- Impl: effects ---

std::vector<EffectInfo> MediaPlayer::Impl::EffectInfos() const {
    std::lock_guard lock(mutex_);
    return effect_manager_.Describe();
}

void MediaPlayer::Impl::SetEffectParam(const std::string& effect_id,
                                       const std::string& param_id, EffectParamValue value) {
    std::lock_guard lock(mutex_);
    if (effect_manager_.SetParam(effect_id, param_id, value)) {
        RefreshLocked();
    }
}

void MediaPlayer::Impl::SetEffectEnabled(const std::string& effect_id, bool enabled) {
    std::lock_guard lock(mutex_);
    if (effect_manager_.SetEnabled(effect_id, enabled)) {
        RefreshLocked();
    }
}

void MediaPlayer::Impl::ResetEffectParams(const std::string& effect_id) {
    std::lock_guard lock(mutex_);
    if (effect_manager_.ResetParams(effect_id)) {
        RefreshLocked();
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

void MediaPlayer::ResetEffectParams(const std::string& effect_id) {
    impl_->ResetEffectParams(effect_id);
}

void MediaPlayer::SetOptions(const PlayerOptions& options) { impl_->SetOptions(options); }
PlayerOptions MediaPlayer::Options() const { return impl_->Options(); }
PipelineInfo MediaPlayer::GetPipelineInfo() const { return impl_->GetPipelineInfo(); }
CapturedFrame MediaPlayer::CaptureFrame() const { return impl_->CaptureFrame(); }

}  // namespace mvp
