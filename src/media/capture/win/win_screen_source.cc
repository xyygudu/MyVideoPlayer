// Windows implementation of capture::ScreenSource: resolves the target area,
// grabs it with DXGI desktop duplication or GDI, and draws the cursor.
#include "capture/screen_source.h"

#include "capture/win/win_util.h"  // First: configures <windows.h>

#include <timeapi.h>

#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include <spdlog/spdlog.h>

#include "capture/win/screen_grabbers.h"

namespace mvp::capture {
namespace {

using win::DesktopCanvas;

constexpr int kMinCaptureSize = 16;

// Encoders want even sizes (4:2:0 chroma); drop the last odd row/column.
DesktopRect EvenSized(DesktopRect rect) {
    rect.width &= ~1;
    rect.height &= ~1;
    return rect;
}

bool Contains(const DesktopRect& outer, const DesktopRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

std::string RectText(const DesktopRect& rect) {
    return std::to_string(rect.width) + "x" + std::to_string(rect.height) + " at (" +
           std::to_string(rect.x) + "," + std::to_string(rect.y) + ")";
}

class WinScreenSource : public ScreenSource {
  public:
    explicit WinScreenSource(VideoCaptureOptions options) : options_(std::move(options)) {}
    ~WinScreenSource() override { Close(); }

    bool Open() override;
    void Close() override;
    int Width() const override { return rect_.width; }
    int Height() const override { return rect_.height; }
    std::string Description() const override;
    bool Grab(uint8_t* dst, int stride) override;

  private:
    bool ResolveTarget();
    std::unique_ptr<win::ScreenGrabber> CreateGrabber();
    /// False when nothing new can be grabbed (the window is minimized/closed).
    bool FollowWindow();

    const VideoCaptureOptions options_;
    std::unique_ptr<win::ScopedDpiAwareness> dpi_;
    bool timer_period_set_{false};
    DesktopRect rect_;                    // Area grabbed next (size fixed at Open)
    std::optional<MonitorInfo> monitor_;  // The monitor holding the whole area
    std::unique_ptr<win::ScreenGrabber> grabber_;
    DesktopCanvas canvas_;
    win::CursorPainter cursor_;
    bool window_lost_{false};
};

bool WinScreenSource::Open() {
    dpi_ = std::make_unique<win::ScopedDpiAwareness>();
    // 1 ms timer resolution so frame ticks are not quantized to 15.6 ms.
    timer_period_set_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    if (!ResolveTarget()) {
        return false;
    }
    if (!canvas_.Create(rect_.width, rect_.height)) {
        return false;
    }
    grabber_ = CreateGrabber();
    return true;
}

void WinScreenSource::Close() {
    grabber_.reset();
    canvas_.Release();
    if (timer_period_set_) {
        timeEndPeriod(1);
        timer_period_set_ = false;
    }
    dpi_.reset();
}

bool WinScreenSource::ResolveTarget() {
    const std::vector<MonitorInfo> monitors = win::EnumerateMonitors();
    DesktopRect rect;
    if (options_.mode == CaptureMode::kMonitor) {
        for (const MonitorInfo& m : monitors) {
            if (m.id == options_.monitor_id || (options_.monitor_id.empty() && m.primary)) {
                rect = m.bounds;
            }
        }
    } else if (options_.mode == CaptureMode::kRegion) {
        rect = options_.region;
    } else if (auto frame = win::WindowFrame(reinterpret_cast<HWND>(options_.window_id))) {
        rect = *frame;
    }
    rect_ = EvenSized(rect);
    if (rect_.width < kMinCaptureSize || rect_.height < kMinCaptureSize) {
        SPDLOG_ERROR("ScreenSource: nothing to capture (target {})", RectText(rect));
        return false;
    }
    for (const MonitorInfo& m : monitors) {
        if (Contains(m.bounds, rect_)) monitor_ = m;
    }
    return true;
}

std::unique_ptr<win::ScreenGrabber> WinScreenSource::CreateGrabber() {
    // A followed window may cross monitors: only GDI can grab any area.
    const bool dxgi_possible = options_.mode != CaptureMode::kWindow && monitor_.has_value();
    if (options_.method != ScreenCaptureMethod::kGdi && dxgi_possible) {
        if (auto dxgi = win::DxgiGrabber::Create(*monitor_, rect_.width, rect_.height)) {
            return dxgi;
        }
    }
    if (options_.method == ScreenCaptureMethod::kDesktopDuplication) {
        SPDLOG_WARN("ScreenSource: desktop duplication unavailable for {}, using GDI", RectText(rect_));
    }
    return std::make_unique<win::GdiGrabber>();
}

std::string WinScreenSource::Description() const {
    std::string text = (grabber_ ? grabber_->Name() : std::string("no grabber")) + ", " + RectText(rect_);
    if (monitor_) text += " on " + monitor_->id;
    return text;
}

bool WinScreenSource::FollowWindow() {
    const auto frame = win::WindowFrame(reinterpret_cast<HWND>(options_.window_id));
    if (!frame) {
        if (!window_lost_) SPDLOG_INFO("ScreenSource: window minimized or closed, holding the last image");
        window_lost_ = true;
        return false;
    }
    window_lost_ = false;
    rect_.x = frame->x;
    rect_.y = frame->y;
    return true;
}

bool WinScreenSource::Grab(uint8_t* dst, int stride) {
    const bool fresh = options_.mode != CaptureMode::kWindow || FollowWindow();
    if (fresh) {
        if (!grabber_->Grab(rect_, &canvas_)) {
            SPDLOG_WARN("ScreenSource: {} stopped working, switching to GDI", grabber_->Name());
            grabber_ = std::make_unique<win::GdiGrabber>();
            grabber_->Grab(rect_, &canvas_);
        }
        if (options_.capture_cursor) {
            cursor_.Paint(rect_, &canvas_, options_.highlight_clicks);
        }
        GdiFlush();
    }
    const size_t row_bytes = static_cast<size_t>(rect_.width) * 4;
    for (int y = 0; y < rect_.height; ++y) {
        std::memcpy(dst + static_cast<ptrdiff_t>(y) * stride,
                    canvas_.bits() + static_cast<ptrdiff_t>(y) * canvas_.stride(), row_bytes);
    }
    return true;
}

}  // namespace

std::unique_ptr<ScreenSource> CreateScreenSource(const VideoCaptureOptions& options) {
    return std::make_unique<WinScreenSource>(options);
}

}  // namespace mvp::capture
