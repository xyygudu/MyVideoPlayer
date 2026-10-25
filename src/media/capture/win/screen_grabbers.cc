#include "capture/win/screen_grabbers.h"

#include <algorithm>
#include <cstring>

#include <spdlog/spdlog.h>

#include "video/pixel_ops.h"

namespace mvp::capture::win {

// --- DesktopCanvas ---

DesktopCanvas::~DesktopCanvas() { Release(); }

bool DesktopCanvas::Create(int width, int height) {
    Release();
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // Top-down rows
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dc_ = CreateCompatibleDC(nullptr);
    bitmap_ = dc_ ? CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!bitmap_ || !bits) {
        SPDLOG_ERROR("DesktopCanvas: cannot allocate a {}x{} bitmap", width, height);
        Release();
        return false;
    }
    previous_ = SelectObject(dc_, bitmap_);
    bits_ = static_cast<uint8_t*>(bits);
    width_ = width;
    height_ = height;
    return true;
}

void DesktopCanvas::Release() {
    if (dc_ && previous_) SelectObject(dc_, previous_);
    if (bitmap_) DeleteObject(bitmap_);
    if (dc_) DeleteDC(dc_);
    dc_ = nullptr;
    bitmap_ = nullptr;
    previous_ = nullptr;
    bits_ = nullptr;
    width_ = height_ = 0;
}

// --- GdiGrabber ---

GdiGrabber::GdiGrabber() : screen_dc_(GetDC(nullptr)) {}

GdiGrabber::~GdiGrabber() {
    if (screen_dc_) ReleaseDC(nullptr, screen_dc_);
}

bool GdiGrabber::Grab(const DesktopRect& rect, DesktopCanvas* canvas) {
    // CAPTUREBLT includes layered windows (menus, tooltips) like ffmpeg's gdigrab.
    if (!BitBlt(canvas->dc(), 0, 0, canvas->width(), canvas->height(), screen_dc_, rect.x, rect.y,
                SRCCOPY | CAPTUREBLT)) {
        // Fails transiently on the secure desktop (UAC, lock screen): keep the last image.
        SPDLOG_DEBUG("GdiGrabber: BitBlt failed ({})", GetLastError());
    }
    return true;
}

// --- DxgiGrabber ---

std::unique_ptr<DxgiGrabber> DxgiGrabber::Create(const MonitorInfo& monitor, int width, int height) {
    std::unique_ptr<DxgiGrabber> grabber(new DxgiGrabber(monitor, width, height));
    if (!grabber->CreateDevice() || grabber->Duplicate() != DuplicateResult::kOk) {
        return nullptr;
    }
    return grabber;
}

DxgiGrabber::DxgiGrabber(MonitorInfo monitor, int width, int height)
    : monitor_(std::move(monitor)), width_(width), height_(height) {}

bool DxgiGrabber::CreateDevice() {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
    // The duplication must come from a device on the adapter driving the output.
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)) || ToUtf8(desc.DeviceName) != monitor_.id) continue;
            if (FAILED(output.As(&output_))) return false;
            const HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                 D3D11_SDK_VERSION, &device_, nullptr, &context_);
            if (FAILED(hr)) {
                SPDLOG_WARN("DxgiGrabber: D3D11CreateDevice failed ({})", HresultString(hr));
                return false;
            }
            D3D11_TEXTURE2D_DESC staging{};
            staging.Width = static_cast<UINT>(width_);
            staging.Height = static_cast<UINT>(height_);
            staging.MipLevels = staging.ArraySize = 1;
            staging.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            staging.SampleDesc.Count = 1;
            staging.Usage = D3D11_USAGE_STAGING;
            staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            return SUCCEEDED(device_->CreateTexture2D(&staging, nullptr, &staging_));
        }
    }
    SPDLOG_WARN("DxgiGrabber: no DXGI output for monitor {}", monitor_.id);
    return false;
}

DxgiGrabber::DuplicateResult DxgiGrabber::Duplicate() {
    duplication_.Reset();
    const HRESULT hr = output_->DuplicateOutput(device_.Get(), &duplication_);
    if (FAILED(hr)) {
        // Access denied while the secure desktop (UAC, lock screen) is up: retry later.
        const bool transient = hr == E_ACCESSDENIED || hr == DXGI_ERROR_SESSION_DISCONNECTED;
        if (!transient) SPDLOG_WARN("DxgiGrabber: DuplicateOutput failed ({})", HresultString(hr));
        return transient ? DuplicateResult::kRetry : DuplicateResult::kUnsupported;
    }
    DXGI_OUTDUPL_DESC desc{};
    duplication_->GetDesc(&desc);
    // A rotated desktop arrives unrotated: GDI handles it. (ModeDesc.Format is
    // the scan-out format; the images themselves are checked when acquired.)
    if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
        SPDLOG_INFO("DxgiGrabber: output {} is rotated ({})", monitor_.id, static_cast<int>(desc.Rotation));
        duplication_.Reset();
        return DuplicateResult::kUnsupported;
    }
    return DuplicateResult::kOk;
}

bool DxgiGrabber::Grab(const DesktopRect& rect, DesktopCanvas* canvas) {
    if (!duplication_ && Duplicate() == DuplicateResult::kUnsupported) {
        return false;
    }
    if (duplication_ && !AcquireLatest(rect)) {
        return false;
    }
    if (has_image_) {
        CopyStagingTo(canvas);
    }
    return true;
}

bool DxgiGrabber::AcquireLatest(const DesktopRect& rect) {
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    // The first frame is the whole desktop; later ones only when it changed.
    const HRESULT hr = duplication_->AcquireNextFrame(has_image_ ? 0 : 200, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return true;
    if (FAILED(hr)) {
        // Desktop switch or mode change (DXGI_ERROR_ACCESS_LOST): duplicate again.
        if (!logged_lost_) SPDLOG_INFO("DxgiGrabber: duplication lost ({}), recreating", HresultString(hr));
        logged_lost_ = true;
        duplication_.Reset();
        return true;
    }
    logged_lost_ = false;
    ComPtr<ID3D11Texture2D> desktop;
    bool supported = true;
    if (SUCCEEDED(resource.As(&desktop))) {
        D3D11_TEXTURE2D_DESC desc{};
        desktop->GetDesc(&desc);
        supported = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
        if (supported) {
            CopyArea(desktop.Get(), desc, rect);
        } else {
            SPDLOG_INFO("DxgiGrabber: desktop image format {} is not 8-bit BGRA", static_cast<int>(desc.Format));
        }
    }
    duplication_->ReleaseFrame();
    return supported;
}

void DxgiGrabber::CopyArea(ID3D11Texture2D* desktop, const D3D11_TEXTURE2D_DESC& desc, const DesktopRect& rect) {
    D3D11_BOX box{};
    box.left = static_cast<UINT>(std::max(0, rect.x - monitor_.bounds.x));
    box.top = static_cast<UINT>(std::max(0, rect.y - monitor_.bounds.y));
    box.right = std::min<UINT>(box.left + static_cast<UINT>(width_), desc.Width);
    box.bottom = std::min<UINT>(box.top + static_cast<UINT>(height_), desc.Height);
    box.back = 1;
    if (box.right > box.left && box.bottom > box.top) {
        context_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0, desktop, 0, &box);
        has_image_ = true;
    }
}

void DxgiGrabber::CopyStagingTo(DesktopCanvas* canvas) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
    const auto* src = static_cast<const uint8_t*>(mapped.pData);
    const size_t row_bytes = static_cast<size_t>(std::min(width_, canvas->width())) * 4;
    const int rows = std::min(height_, canvas->height());
    for (int y = 0; y < rows; ++y) {
        std::memcpy(canvas->bits() + static_cast<ptrdiff_t>(y) * canvas->stride(),
                    src + static_cast<size_t>(y) * mapped.RowPitch, row_bytes);
    }
    context_->Unmap(staging_.Get(), 0);
}

// --- CursorPainter ---

void CursorPainter::Paint(const DesktopRect& rect, DesktopCanvas* canvas, bool highlight_clicks) {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info) || !(info.flags & CURSOR_SHOWING) || !info.hCursor) {
        return;
    }
    const int x = info.ptScreenPos.x - rect.x;
    const int y = info.ptScreenPos.y - rect.y;
    if (highlight_clicks) {
        PaintClickHalo(x, y, canvas);  // Under the cursor
    }
    const POINT hotspot = Hotspot(info.hCursor);
    DrawIconEx(canvas->dc(), x - hotspot.x, y - hotspot.y, info.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    GdiFlush();
}

void CursorPainter::PaintClickHalo(int x, int y, DesktopCanvas* canvas) {
    const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (!left && !right) {
        return;
    }
    GdiFlush();  // The grabbed pixels must be in memory before touching them.
    const int radius = MulDiv(18, static_cast<int>(GetDpiForSystem()), 96);
    const pixel_ops::BgraImage image{canvas->bits(), canvas->width(), canvas->height(),
                                     canvas->stride()};
    pixel_ops::BlendDiscBgra(image, x, y, radius, left ? 0xFFC400u : 0x4AA3FFu, 0.45f);
}

POINT CursorPainter::Hotspot(HCURSOR cursor) {
    if (cursor == cached_cursor_) {
        return cached_hotspot_;
    }
    ICONINFO icon{};
    POINT hotspot{0, 0};
    if (GetIconInfo(cursor, &icon)) {
        hotspot = {static_cast<LONG>(icon.xHotspot), static_cast<LONG>(icon.yHotspot)};
        if (icon.hbmMask) DeleteObject(icon.hbmMask);
        if (icon.hbmColor) DeleteObject(icon.hbmColor);
    }
    cached_cursor_ = cursor;
    cached_hotspot_ = hotspot;
    return hotspot;
}

}  // namespace mvp::capture::win
