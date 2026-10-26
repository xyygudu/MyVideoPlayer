// Windows implementation of capture::CameraSource on the Media Foundation source
// reader in asynchronous mode, which decodes (MJPEG) and converts the camera
// output to RGB32.
#include "capture/win/mf_camera_source.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <wrl/implements.h>

#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>

#include <spdlog/spdlog.h>

#include "capture/win/media_foundation.h"

namespace mvp::capture {
namespace {

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

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

// The capture device `device_id` (the first camera when empty).
ComPtr<IMFMediaSource> OpenCaptureDevice(const std::string& device_id, std::string* name) {
    std::string id = device_id;
    for (const CameraInfo& camera : win::EnumerateCameras()) {
        if (id.empty() || camera.id == id) {
            id = camera.id;
            *name = camera.name;
            break;
        }
    }
    if (id.empty()) {
        SPDLOG_WARN("MfCameraSource: no camera connected");
        return nullptr;
    }
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 2);
    if (SUCCEEDED(hr)) {
        hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }
    if (SUCCEEDED(hr)) {
        hr = attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, win::ToWide(id).c_str());
    }
    ComPtr<IMFMediaSource> source;
    if (SUCCEEDED(hr)) hr = MFCreateDeviceSource(attributes.Get(), &source);
    if (FAILED(hr)) {
        SPDLOG_WARN("MfCameraSource: opening the camera failed for '{}' ({})", *name, win::HresultString(hr));
        return nullptr;
    }
    return source;
}

// Async reads: a synchronous ReadSample cannot be cancelled, not even by shutting the source down.
// One per reader (which keeps it alive, so late results land here). Written by Media Foundation
// worker threads and Interrupt(), read by the capture thread.
class ReadCallback : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMFSourceReaderCallback> {
  public:
    struct Result {
        HRESULT status{S_OK};
        DWORD flags{0};
        ComPtr<IMFSample> sample;  // Null for a stream tick or an error
    };

    STDMETHODIMP OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ = Result{status, flags, sample};
        }
        ready_.notify_all();
        return S_OK;
    }
    STDMETHODIMP OnFlush(DWORD) override { return S_OK; }
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }

    /// The result of the pending request; nullopt once interrupted.
    std::optional<Result> Wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return result_.has_value() || interrupted_; });
        if (interrupted_) return std::nullopt;
        return std::exchange(result_, std::nullopt);
    }

    /// Ends the current Wait() and makes every later one return at once.
    void Interrupt() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            interrupted_ = true;
        }
        ready_.notify_all();
    }

  private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::optional<Result> result_;
    bool interrupted_{false};
};

class MfCameraSource : public CameraSource {
  public:
    explicit MfCameraSource(win::MediaSourceFactory factory) : factory_(std::move(factory)) {}
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
    bool Interrupted();
    ReadStatus Deliver(const ReadCallback::Result& result, uint8_t* dst, int stride);
    bool CopySample(IMFSample* sample, uint8_t* dst, int stride) const;
    bool Failed(const char* what, HRESULT hr) const;

    const win::MediaSourceFactory factory_;
    std::unique_ptr<win::ScopedCom> com_;
    std::unique_ptr<win::ScopedMediaFoundation> mf_;
    ComPtr<IMFMediaSource> source_;
    ComPtr<IMFSourceReader> reader_;
    // Guards callback_ (written on the capture thread) and interrupted_ against Interrupt().
    std::mutex callback_mutex_;
    ComPtr<ReadCallback> callback_;
    bool interrupted_{false};   // Sticky: once interrupted, every Read returns at once
    bool read_pending_{false};  // Capture thread: a request awaits its result in callback_
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
    source_ = factory_(&name_);
    return source_ != nullptr;
}

bool MfCameraSource::CreateReader() {
    ComPtr<ReadCallback> callback = Make<ReadCallback>();
    if (!callback) return Failed("creating the read callback", E_OUTOFMEMORY);
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (interrupted_) callback->Interrupt();
        callback_ = callback;
    }
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 2);
    // Lets the reader decode MJPEG and convert YUV to RGB32 for us.
    if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (SUCCEEDED(hr)) {
        hr = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,
                                    static_cast<IMFSourceReaderCallback*>(callback.Get()));
    }
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
    // Releasing the reader drops a pending request; a late result only reaches its callback.
    reader_.Reset();
    read_pending_ = false;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback_.Reset();
    }
    if (source_) source_->Shutdown();
    source_.Reset();
    mf_.reset();
    com_.reset();
}

void MfCameraSource::Interrupt() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    interrupted_ = true;
    if (callback_) callback_->Interrupt();
}

bool MfCameraSource::Interrupted() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    return interrupted_;
}

std::string MfCameraSource::Description() const {
    return "'" + name_ + "', " + std::to_string(width_) + "x" + std::to_string(height_) + " at " +
           std::to_string(static_cast<int>(std::lround(fps_))) + " fps";
}

CameraSource::ReadStatus MfCameraSource::Read(uint8_t* dst, int stride) {
    if (!read_pending_) {
        if (Interrupted()) return ReadStatus::kNoFrame;
        const HRESULT hr = reader_->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr)) {
            Failed("requesting a frame", hr);
            return ReadStatus::kFailed;
        }
        read_pending_ = true;
    }
    const std::optional<ReadCallback::Result> result = callback_->Wait();
    if (!result) return ReadStatus::kNoFrame;  // Interrupted: Close() drops the pending request
    read_pending_ = false;
    return Deliver(*result, dst, stride);
}

CameraSource::ReadStatus MfCameraSource::Deliver(const ReadCallback::Result& result, uint8_t* dst, int stride) {
    if (FAILED(result.status) || (result.flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
        Failed("reading a frame", FAILED(result.status) ? result.status : MF_E_END_OF_STREAM);
        return ReadStatus::kFailed;
    }
    if (result.flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
        SPDLOG_INFO("MfCameraSource: '{}' changed its format, reopening", name_);
        return ReadStatus::kFailed;
    }
    if (!result.sample) return ReadStatus::kNoFrame;  // Stream tick (gap)
    return CopySample(result.sample.Get(), dst, stride) ? ReadStatus::kFrame : ReadStatus::kNoFrame;
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

namespace win {

std::unique_ptr<CameraSource> CreateMfCameraSource(MediaSourceFactory factory) {
    return std::make_unique<MfCameraSource>(std::move(factory));
}

}  // namespace win

std::unique_ptr<CameraSource> CreateCameraSource(const std::string& device_id) {
    return win::CreateMfCameraSource(
        [device_id](std::string* name) { return OpenCaptureDevice(device_id, name); });
}

}  // namespace mvp::capture
