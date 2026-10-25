// Windows implementation of capture::CameraSource on the Media Foundation
// source reader, which decodes (MJPEG) and converts the camera output to RGB32.
#include "capture/camera_source.h"

#include "capture/win/win_util.h"  // First: configures <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

#include <spdlog/spdlog.h>

#include "capture/win/media_foundation.h"

namespace mvp::capture {
namespace {

using Microsoft::WRL::ComPtr;

constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
// The overlay is small: a 720p camera mode is plenty and cheap to convert.
constexpr double kPreferredArea = 1280.0 * 720.0;
constexpr double kMinFrameRate = 24.0;

struct NativeMode {
    UINT32 width{0};
    UINT32 height{0};
    double fps{0};
};

NativeMode ReadMode(IMFMediaType* type) {
    NativeMode mode;
    UINT32 num = 0;
    UINT32 den = 0;
    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &mode.width, &mode.height);
    if (SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den)) && den != 0) {
        mode.fps = static_cast<double>(num) / den;
    }
    return mode;
}

// Lower is better: smooth first, then the size nearest to 720p.
double ModeCost(const NativeMode& mode) {
    const double area_cost = std::abs(static_cast<double>(mode.width) * mode.height - kPreferredArea);
    return area_cost + (mode.fps < kMinFrameRate ? 1e12 : 0.0);
}

class MfCameraSource : public CameraSource {
  public:
    explicit MfCameraSource(std::string device_id) : device_id_(std::move(device_id)) {}
    ~MfCameraSource() override { Close(); }

    bool Open() override;
    void Close() override;
    int Width() const override { return width_; }
    int Height() const override { return height_; }
    double FrameRate() const override { return fps_; }
    std::string Description() const override;
    ReadStatus Read(uint8_t* dst, int stride) override;
    void Interrupt() override;

  private:
    bool CreateSource();
    bool CreateReader();
    bool SelectNativeMode();
    bool SelectOutputType();
    bool CopySample(IMFSample* sample, uint8_t* dst, int stride) const;
    bool Failed(const char* what, HRESULT hr) const;

    const std::string device_id_;
    std::unique_ptr<win::ScopedCom> com_;
    std::unique_ptr<win::ScopedMediaFoundation> mf_;
    std::mutex source_mutex_;  // Guards source_ and interrupted_ against Interrupt()
    ComPtr<IMFMediaSource> source_;
    bool interrupted_{false};  // Sticky: once interrupted, every Read returns at once
    ComPtr<IMFSourceReader> reader_;
    std::string name_;
    int width_{0};
    int height_{0};
    double fps_{0};
    LONG default_stride_{0};
};

bool MfCameraSource::Open() {
    Close();
    com_ = std::make_unique<win::ScopedCom>();
    mf_ = std::make_unique<win::ScopedMediaFoundation>();
    if (!com_->Ok() || !mf_->Ok()) {
        SPDLOG_ERROR("MfCameraSource: COM or Media Foundation unavailable");
        Close();
        return false;
    }
    if (!CreateSource() || !CreateReader() || !SelectNativeMode() || !SelectOutputType()) {
        Close();
        return false;
    }
    SPDLOG_INFO("MfCameraSource: {}", Description());
    return true;
}

bool MfCameraSource::CreateSource() {
    std::string id = device_id_;
    for (const CameraInfo& camera : win::EnumerateCameras()) {
        if (id.empty() || camera.id == id) {
            id = camera.id;
            name_ = camera.name;
            break;
        }
    }
    if (id.empty()) {
        SPDLOG_WARN("MfCameraSource: no camera connected");
        return false;
    }
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 2);
    if (SUCCEEDED(hr)) {
        attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, win::ToWide(id).c_str());
        ComPtr<IMFMediaSource> source;
        hr = MFCreateDeviceSource(attributes.Get(), &source);
        std::lock_guard<std::mutex> lock(source_mutex_);
        source_ = source;
        if (interrupted_ && source_) {
            source_->Shutdown();  // Interrupted while opening: reads must not block
        }
    }
    return SUCCEEDED(hr) || Failed("opening the camera", hr);
}

bool MfCameraSource::CreateReader() {
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    // Lets the reader decode MJPEG and convert YUV to RGB32 for us.
    if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source_.Get(), attributes.Get(), &reader_);
    return SUCCEEDED(hr) || Failed("creating the source reader", hr);
}

bool MfCameraSource::SelectNativeMode() {
    ComPtr<IMFMediaType> best;
    double best_cost = std::numeric_limits<double>::max();
    ComPtr<IMFMediaType> type;
    for (DWORD i = 0; SUCCEEDED(reader_->GetNativeMediaType(kVideoStream, i, &type)); ++i) {
        const NativeMode mode = ReadMode(type.Get());
        if (mode.width == 0 || mode.height == 0) continue;
        const double cost = ModeCost(mode);
        if (cost < best_cost) {
            best_cost = cost;
            best = type;
        }
    }
    if (!best) {
        SPDLOG_ERROR("MfCameraSource: '{}' offers no video mode", name_);
        return false;
    }
    const HRESULT hr = reader_->SetCurrentMediaType(kVideoStream, nullptr, best.Get());
    if (FAILED(hr)) return Failed("selecting the camera mode", hr);
    fps_ = ReadMode(best.Get()).fps;
    return true;
}

bool MfCameraSource::SelectOutputType() {
    ComPtr<IMFMediaType> output;
    HRESULT hr = MFCreateMediaType(&output);
    if (SUCCEEDED(hr)) hr = output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = reader_->SetCurrentMediaType(kVideoStream, nullptr, output.Get());
    ComPtr<IMFMediaType> current;
    if (SUCCEEDED(hr)) hr = reader_->GetCurrentMediaType(kVideoStream, &current);
    if (FAILED(hr)) return Failed("converting the camera output to RGB32", hr);
    const NativeMode mode = ReadMode(current.Get());
    width_ = static_cast<int>(mode.width);
    height_ = static_cast<int>(mode.height);
    UINT32 stride = 0;
    // Signed: negative for bottom-up rows. Only used when the buffer is not 2D.
    default_stride_ = SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))
                          ? static_cast<LONG>(static_cast<INT32>(stride))
                          : width_ * 4;
    return width_ > 0 && height_ > 0;
}

void MfCameraSource::Close() {
    reader_.Reset();
    {
        std::lock_guard<std::mutex> lock(source_mutex_);
        if (source_) source_->Shutdown();
        source_.Reset();
    }
    mf_.reset();
    com_.reset();
}

void MfCameraSource::Interrupt() {
    std::lock_guard<std::mutex> lock(source_mutex_);
    interrupted_ = true;
    // A blocked ReadSample returns MF_E_SHUTDOWN once the source is shut down.
    if (source_) source_->Shutdown();
}

std::string MfCameraSource::Description() const {
    return "'" + name_ + "', " + std::to_string(width_) + "x" + std::to_string(height_) + " at " +
           std::to_string(static_cast<int>(std::lround(fps_))) + " fps";
}

CameraSource::ReadStatus MfCameraSource::Read(uint8_t* dst, int stride) {
    {
        std::lock_guard<std::mutex> lock(source_mutex_);
        if (interrupted_) return ReadStatus::kNoFrame;
    }
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    ComPtr<IMFSample> sample;
    const HRESULT hr = reader_->ReadSample(kVideoStream, 0, nullptr, &flags, &timestamp, &sample);
    {
        std::lock_guard<std::mutex> lock(source_mutex_);
        if (interrupted_) return ReadStatus::kNoFrame;
    }
    if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
        Failed("reading a frame", FAILED(hr) ? hr : MF_E_END_OF_STREAM);
        return ReadStatus::kFailed;
    }
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
        SPDLOG_INFO("MfCameraSource: '{}' changed its format, reopening", name_);
        return ReadStatus::kFailed;
    }
    if (!sample) return ReadStatus::kNoFrame;  // Stream tick (gap)
    return CopySample(sample.Get(), dst, stride) ? ReadStatus::kFrame : ReadStatus::kNoFrame;
}

bool MfCameraSource::CopySample(IMFSample* sample, uint8_t* dst, int stride) const {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
    // RGB32 rows may be stored bottom-up: walk them with the signed pitch.
    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    ComPtr<IMF2DBuffer> buffer_2d;
    BYTE* data = nullptr;
    const bool locked_2d = SUCCEEDED(buffer.As(&buffer_2d)) && SUCCEEDED(buffer_2d->Lock2D(&scan0, &pitch));
    if (!locked_2d) {
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length))) return false;
        pitch = default_stride_;
        scan0 = pitch < 0 ? data + static_cast<ptrdiff_t>(-pitch) * (height_ - 1) : data;
    }
    for (int y = 0; y < height_; ++y) {
        std::memcpy(dst + static_cast<ptrdiff_t>(y) * stride, scan0 + static_cast<ptrdiff_t>(y) * pitch,
                    static_cast<size_t>(width_) * 4);
    }
    if (locked_2d) {
        buffer_2d->Unlock2D();
    } else {
        buffer->Unlock();
    }
    return true;
}

bool MfCameraSource::Failed(const char* what, HRESULT hr) const {
    SPDLOG_WARN("MfCameraSource: {} failed for '{}' ({})", what, name_, win::HresultString(hr));
    return false;
}

}  // namespace

std::unique_ptr<CameraSource> CreateCameraSource(const std::string& device_id) {
    return std::make_unique<MfCameraSource>(device_id);
}

}  // namespace mvp::capture
