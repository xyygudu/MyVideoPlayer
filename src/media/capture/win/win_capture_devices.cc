// Windows implementation of the platform half of mvp::CaptureDevices.
#include "mvp/capture_devices.h"

#include "capture/win/win_util.h"  // First: configures <windows.h>

#include <dwmapi.h>

#include <string>
#include <vector>

#include "capture/win/audio_endpoints.h"
#include "capture/win/media_foundation.h"

namespace mvp {
namespace {

constexpr int kMinWindowSize = 16;

// What a user would call "a window": visible, titled, not a tool palette and
// not cloaked (hidden UWP frames, windows on other virtual desktops).
bool IsCapturableWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || GetWindowTextLengthW(hwnd) == 0) {
        return false;
    }
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
        return false;
    }
    DWORD cloaked = 0;
    return FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked == 0;
}

std::string WindowTitle(HWND hwnd) {
    std::wstring title(static_cast<size_t>(GetWindowTextLengthW(hwnd)) + 1, L'\0');
    const int length = GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
    title.resize(static_cast<size_t>(length > 0 ? length : 0));
    return capture::win::ToUtf8(title.c_str());
}

BOOL CALLBACK CollectWindow(HWND hwnd, LPARAM param) {
    if (!IsCapturableWindow(hwnd)) {
        return TRUE;
    }
    const auto frame = capture::win::WindowFrame(hwnd);
    if (!frame || frame->width < kMinWindowSize || frame->height < kMinWindowSize) {
        return TRUE;
    }
    WindowInfo info;
    info.id = reinterpret_cast<uint64_t>(hwnd);
    info.title = WindowTitle(hwnd);
    info.bounds = *frame;
    reinterpret_cast<std::vector<WindowInfo>*>(param)->push_back(std::move(info));
    return TRUE;
}

}  // namespace

std::vector<MonitorInfo> CaptureDevices::Monitors() {
    capture::win::ScopedDpiAwareness dpi;
    return capture::win::EnumerateMonitors();
}

std::vector<WindowInfo> CaptureDevices::Windows() {
    capture::win::ScopedDpiAwareness dpi;
    std::vector<WindowInfo> windows;
    EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&windows));  // Z order, topmost first
    return windows;
}

std::optional<DesktopRect> CaptureDevices::WindowBounds(uint64_t window_id) {
    capture::win::ScopedDpiAwareness dpi;
    return capture::win::WindowFrame(reinterpret_cast<HWND>(window_id));
}

std::vector<CameraInfo> CaptureDevices::Cameras() {
    capture::win::ScopedCom com;
    return com.Ok() ? capture::win::EnumerateCameras() : std::vector<CameraInfo>{};
}

std::vector<AudioDeviceInfo> CaptureDevices::AudioDevices(AudioDeviceKind kind) {
    capture::win::ScopedCom com;
    return com.Ok() ? capture::win::EnumerateAudioEndpoints(kind) : std::vector<AudioDeviceInfo>{};
}

}  // namespace mvp
