#include "recording_controller.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <spdlog/spdlog.h>

#include "countdown_overlay.h"
#include "desktop_mapping.h"
#include "recording_toolbar.h"
#include "region_frame.h"

namespace {

constexpr int kStatsIntervalMs = 250;

}  // namespace

RecordingController::RecordingController(QObject* parent)
    : QObject(parent),
      countdown_(std::make_unique<CountdownOverlay>()),
      frame_(std::make_unique<RegionFrame>()),
      toolbar_(std::make_unique<RecordingToolbar>()) {
    stats_timer_ = new QTimer(this);
    stats_timer_->setInterval(kStatsIntervalMs);
    connect(stats_timer_, &QTimer::timeout, this, &RecordingController::OnStatsTick);
    connect(countdown_.get(), &CountdownOverlay::Finished, this, &RecordingController::BeginRecording);
    connect(toolbar_.get(), &RecordingToolbar::PauseResumeClicked, this, &RecordingController::TogglePause);
    connect(toolbar_.get(), &RecordingToolbar::StopClicked, this, &RecordingController::Stop);
}

RecordingController::~RecordingController() {
    // First: joins the thread that may be posting OnCompleted() to this object.
    recorder_.reset();
}

void RecordingController::Start(const mvp::RecordOptions& options, const Presentation& presentation) {
    if (phase_ != Phase::kIdle) {
        return;
    }
    options_ = options;
    presentation_ = presentation;
    if (presentation.countdown_seconds > 0) {
        SetPhase(Phase::kCountdown);
        countdown_->Start(TargetScreen(), presentation.countdown_seconds);
        return;
    }
    BeginRecording();
}

void RecordingController::BeginRecording() {
    recorder_ = std::make_unique<mvp::Recorder>();
    // Runs on the recorder's thread; the destructor joins it before this dies.
    recorder_->SetCompletionCallback([this](mvp::RecordResult result) {
        QMetaObject::invokeMethod(this, [this, result] { OnCompleted(result); }, Qt::QueuedConnection);
    });
    if (!recorder_->Start(options_)) {
        recorder_.reset();
        SetPhase(Phase::kIdle);
        emit StartFailed();
        return;
    }
    SetPhase(Phase::kRecording);
    ShowHelpers();
    stats_timer_->start();
}

void RecordingController::TogglePause() {
    if (phase_ == Phase::kRecording) {
        recorder_->Pause();
        SetPhase(Phase::kPaused);
    } else if (phase_ == Phase::kPaused) {
        recorder_->Resume();
        SetPhase(Phase::kRecording);
    } else {
        return;
    }
    const bool paused = phase_ == Phase::kPaused;
    frame_->SetPaused(paused);
    toolbar_->SetPaused(paused);
}

void RecordingController::Stop() {
    if (phase_ == Phase::kCountdown) {
        countdown_->Cancel();
        SetPhase(Phase::kIdle);
    } else if (phase_ == Phase::kRecording || phase_ == Phase::kPaused) {
        recorder_->Stop();
        SetPhase(Phase::kStopping);
    }
}

mvp::RecordingSources RecordingController::Sources() const {
    return recorder_ ? recorder_->Sources() : mvp::RecordingSources{};
}

void RecordingController::SetVolume(mvp::AudioSourceKind source, float volume) {
    if (recorder_) {
        recorder_->SetVolume(source, volume);
    }
}

void RecordingController::ShowHelpers() {
    if (presentation_.outline) {
        frame_->SetPaused(false);
        frame_->ShowAround(presentation_.area);
    }
    toolbar_->SetPaused(false);
    toolbar_->SetElapsed(0.0);
    toolbar_->ShowOn(TargetScreen());
}

void RecordingController::HideHelpers() {
    stats_timer_->stop();
    frame_->hide();
    toolbar_->hide();
}

void RecordingController::OnStatsTick() {
    const mvp::RecordingStats stats = recorder_->Stats();
    toolbar_->SetElapsed(stats.elapsed_seconds);
    if (presentation_.outline && presentation_.follow_window != 0) {
        if (const auto bounds = mvp::CaptureDevices::WindowBounds(presentation_.follow_window)) {
            frame_->ShowAround({bounds->x, bounds->y, presentation_.area.width, presentation_.area.height});
        }
    }
    emit StatsUpdated(stats);
}

void RecordingController::OnCompleted(mvp::RecordResult result) {
    HideHelpers();
    const bool succeeded = result == mvp::RecordResult::kSucceeded;
    SPDLOG_INFO("UI: recording finished ({})", succeeded ? "succeeded" : "failed");
    SetPhase(Phase::kIdle);
    emit Finished(QString::fromStdString(options_.output_path), succeeded);
}

void RecordingController::SetPhase(Phase phase) {
    if (phase_ != phase) {
        phase_ = phase;
        emit PhaseChanged(phase);
    }
}

QScreen* RecordingController::TargetScreen() const {
    const auto screen = desktop_mapping::ScreenAt(presentation_.area);
    return screen ? screen->screen : QGuiApplication::primaryScreen();
}
