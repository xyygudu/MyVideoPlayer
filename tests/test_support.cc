#include "test_support.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace mvp::test {
namespace {

constexpr wchar_t kWindowClass[] = L"MvpTestWindow";

void PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool RegisterWindowClass() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    return RegisterClassW(&wc) != 0;
}

}  // namespace

std::string MediaPath(const std::string& name) {
    return std::string(MVP_TEST_MEDIA_DIR) + "/" + name;
}

std::string ScratchPath(const std::string& name) {
    const std::filesystem::path dir = std::filesystem::path(MVP_TEST_MEDIA_DIR) / "out";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / name;
    std::filesystem::remove(path);
    return path.string();
}

std::string GetEnv(const char* name) {
    const DWORD size = GetEnvironmentVariableA(name, nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::string value(size, '\0');
    const DWORD written = GetEnvironmentVariableA(name, value.data(), size);
    value.resize(written);
    return value;
}

bool WaitUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        PumpMessages();
        if (done()) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void PumpFor(std::chrono::milliseconds duration) {
    WaitUntil([] { return false; }, duration);
}

double MeanLuma(const CapturedFrame& frame) {
    if (frame.width == 0 || frame.rgba.empty()) {
        return -1.0;
    }
    double sum = 0.0;
    for (size_t i = 0; i + 3 < frame.rgba.size(); i += 4) {
        sum += frame.rgba[i] + frame.rgba[i + 1] + frame.rgba[i + 2];
    }
    return sum / (3.0 * (frame.rgba.size() / 4));
}

double MeanAbsDiff(const CapturedFrame& a, const CapturedFrame& b) {
    if (a.width != b.width || a.height != b.height || a.rgba.empty() || a.rgba.size() != b.rgba.size()) {
        return 255.0;
    }
    double sum = 0.0;
    for (size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        for (size_t c = 0; c < 3; ++c) {
            sum += std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
        }
    }
    return sum / (3.0 * (a.rgba.size() / 4));
}

CapturedFrame Mirrored(const CapturedFrame& frame) {
    CapturedFrame mirror = frame;
    for (int y = 0; y < frame.height; ++y) {
        const size_t row = static_cast<size_t>(y) * frame.width * 4;
        for (int x = 0; x < frame.width; ++x) {
            const size_t from = row + static_cast<size_t>(frame.width - 1 - x) * 4;
            std::copy_n(&frame.rgba[from], 4, &mirror.rgba[row + static_cast<size_t>(x) * 4]);
        }
    }
    return mirror;
}

void SaveImage(const CapturedFrame& frame, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (size_t i = 0; i + 3 < frame.rgba.size(); i += 4) {
        out.write(reinterpret_cast<const char*>(&frame.rgba[i]), 3);
    }
}

HiddenWindow::HiddenWindow(int width, int height) {
    static const bool registered = RegisterWindowClass();
    if (!registered) {
        return;
    }
    hwnd_ = CreateWindowExW(0, kWindowClass, L"mvp_tests", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, width, height, nullptr, nullptr, GetModuleHandleW(nullptr),
                            nullptr);
}

HiddenWindow::~HiddenWindow() {
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
    }
}

}  // namespace mvp::test
