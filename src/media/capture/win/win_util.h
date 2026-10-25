#ifndef MVP_CAPTURE_WIN_WIN_UTIL_H_
#define MVP_CAPTURE_WIN_WIN_UTIL_H_

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mvp/capture_devices.h"

namespace mvp::capture::win {

std::string ToUtf8(const wchar_t* text);
std::wstring ToWide(const std::string& utf8);
/// "0x887A0026", for logs.
std::string HresultString(HRESULT hr);

/// COM on the calling thread for the scope. Joins the MTA, or keeps the
/// apartment the thread already has (a Qt GUI thread is an STA).
class ScopedCom {
  public:
    ScopedCom() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ScopedCom() {
        if (SUCCEEDED(hr_)) CoUninitialize();
    }
    ScopedCom(const ScopedCom&) = delete;
    ScopedCom& operator=(const ScopedCom&) = delete;

    bool Ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

  private:
    HRESULT hr_;
};

/// Per-monitor DPI awareness for the calling thread for the scope, so window,
/// monitor and cursor APIs speak physical pixels whatever the process mode.
class ScopedDpiAwareness {
  public:
    ScopedDpiAwareness()
        : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~ScopedDpiAwareness() {
        if (previous_) SetThreadDpiAwarenessContext(previous_);
    }
    ScopedDpiAwareness(const ScopedDpiAwareness&) = delete;
    ScopedDpiAwareness& operator=(const ScopedDpiAwareness&) = delete;

  private:
    DPI_AWARENESS_CONTEXT previous_;
};

DesktopRect ToDesktopRect(const RECT& rect);

/// Every monitor in physical pixels (call with per-monitor DPI awareness).
std::vector<MonitorInfo> EnumerateMonitors();

/// The window's visible frame (DWM bounds, no drop shadow) in physical
/// pixels; nullopt when it no longer exists or is minimized.
std::optional<DesktopRect> WindowFrame(HWND hwnd);

/// QueryPerformanceCounter in 100-ns units, the unit WASAPI timestamps use.
int64_t QpcNow100ns();

}  // namespace mvp::capture::win

#endif  // MVP_CAPTURE_WIN_WIN_UTIL_H_
