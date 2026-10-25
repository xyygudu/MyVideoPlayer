#include "capture/win/win_util.h"

#include <dwmapi.h>

#include <cstdio>

namespace mvp::capture::win {

std::string ToUtf8(const wchar_t* text) {
    if (!text || !*text) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (size <= 1) {
        return {};
    }
    std::wstring out(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), size);
    return out;
}

std::string HresultString(HRESULT hr) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(hr));
    return text;
}

DesktopRect ToDesktopRect(const RECT& rect) {
    return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}

namespace {

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(param);
        list->push_back({ToUtf8(info.szDevice), ToDesktopRect(info.rcMonitor),
                         (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
    }
    return TRUE;
}

}  // namespace

std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, &AddMonitor, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

std::optional<DesktopRect> WindowFrame(HWND hwnd) {
    if (!IsWindow(hwnd) || IsIconic(hwnd)) {
        return std::nullopt;
    }
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))) &&
        !GetWindowRect(hwnd, &rect)) {
        return std::nullopt;
    }
    return ToDesktopRect(rect);
}

int64_t QpcNow100ns() {
    static const int64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // Split to avoid overflowing 64 bits for long uptimes.
    const int64_t seconds = now.QuadPart / frequency;
    const int64_t rest = now.QuadPart % frequency;
    return seconds * 10'000'000 + rest * 10'000'000 / frequency;
}

}  // namespace mvp::capture::win
