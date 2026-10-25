#ifndef MVP_CAPTURE_WIN_SCREEN_GRABBERS_H_
#define MVP_CAPTURE_WIN_SCREEN_GRABBERS_H_

#include "capture/win/win_util.h"  // First: configures <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace mvp::capture::win {

/// A BGRA image in a top-down DIB section: grabbers write it and GDI can
/// draw on it (the cursor). Used on one thread.
class DesktopCanvas {
  public:
    DesktopCanvas() = default;
    ~DesktopCanvas();
    DesktopCanvas(const DesktopCanvas&) = delete;
    DesktopCanvas& operator=(const DesktopCanvas&) = delete;

    bool Create(int width, int height);
    void Release();

    HDC dc() const { return dc_; }
    uint8_t* bits() const { return bits_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return width_ * 4; }

  private:
    HDC dc_{nullptr};
    HBITMAP bitmap_{nullptr};
    HGDIOBJ previous_{nullptr};
    uint8_t* bits_{nullptr};
    int width_{0};
    int height_{0};
};

/// Copies an area of the desktop into a canvas of the area's size.
class ScreenGrabber {
  public:
    virtual ~ScreenGrabber() = default;
    /// False when this grabber cannot go on (the caller switches to GDI).
    virtual bool Grab(const DesktopRect& rect, DesktopCanvas* canvas) = 0;
    virtual std::string Name() const = 0;
};

/// GDI BitBlt from the screen: works for any area, layered windows
/// included, at a higher CPU cost (ffmpeg's gdigrab).
class GdiGrabber : public ScreenGrabber {
  public:
    GdiGrabber();
    ~GdiGrabber() override;
    bool Grab(const DesktopRect& rect, DesktopCanvas* canvas) override;
    std::string Name() const override { return "GDI"; }

  private:
    HDC screen_dc_{nullptr};
};

/// DXGI desktop duplication of one monitor (OBS display capture, ffmpeg
/// ddagrab): the compositor hands out the desktop as a GPU texture, the
/// requested area is copied to a staging texture and read back. Frames only
/// arrive when the screen changes, so the last image is kept and repeated.
class DxgiGrabber : public ScreenGrabber {
  public:
    /// Null when duplication is unavailable (rotated or HDR output, remote
    /// session, hybrid GPU without access) - use GDI then.
    static std::unique_ptr<DxgiGrabber> Create(const MonitorInfo& monitor, int width, int height);

    bool Grab(const DesktopRect& rect, DesktopCanvas* canvas) override;
    std::string Name() const override { return "DXGI desktop duplication"; }

  private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    enum class DuplicateResult { kOk, kRetry, kUnsupported };

    DxgiGrabber(MonitorInfo monitor, int width, int height);
    bool CreateDevice();
    DuplicateResult Duplicate();
    /// Takes the newest desktop image, if any; false when its format is unusable.
    bool AcquireLatest(const DesktopRect& rect);
    void CopyArea(ID3D11Texture2D* desktop, const D3D11_TEXTURE2D_DESC& desc, const DesktopRect& rect);
    void CopyStagingTo(DesktopCanvas* canvas);

    MonitorInfo monitor_;
    int width_;
    int height_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutput1> output_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> staging_;  // The latest image of the area
    bool has_image_{false};
    bool logged_lost_{false};
};

/// Draws the mouse cursor (and a halo while a button is held) onto a canvas.
class CursorPainter {
  public:
    void Paint(const DesktopRect& rect, DesktopCanvas* canvas, bool highlight_clicks);

  private:
    void PaintClickHalo(int x, int y, DesktopCanvas* canvas);
    POINT Hotspot(HCURSOR cursor);

    HCURSOR cached_cursor_{nullptr};
    POINT cached_hotspot_{0, 0};
};

}  // namespace mvp::capture::win

#endif  // MVP_CAPTURE_WIN_SCREEN_GRABBERS_H_
