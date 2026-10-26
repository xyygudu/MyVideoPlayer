// MfCameraSource against a fake Media Foundation camera, so the capture path runs without a device.
#ifdef _WIN32

#include "capture/win/mf_camera_source.h"  // First: configures <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <wrl/implements.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace mvp::capture {
namespace {

using Microsoft::WRL::ChainInterfaces;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using ReadStatus = CameraSource::ReadStatus;
using namespace std::chrono_literals;

constexpr UINT32 kWidth = 64;
constexpr UINT32 kHeight = 48;
constexpr UINT32 kFrameRate = 30;
constexpr LONGLONG kFrameDuration = 10'000'000 / kFrameRate;
constexpr uint8_t kPixel[4] = {0x10, 0x20, 0x30, 0xFF};  // Every pixel of a delivered frame, BGRA
constexpr auto kHangTimeout = 5s;

HRESULT CreateFrameType(IMFMediaType** type) {
    ComPtr<IMFMediaType> frame_type;
    HRESULT hr = MFCreateMediaType(&frame_type);
    if (SUCCEEDED(hr)) hr = frame_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = frame_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(frame_type.Get(), MF_MT_FRAME_SIZE, kWidth, kHeight);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(frame_type.Get(), MF_MT_FRAME_RATE, kFrameRate, 1);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(frame_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = frame_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = frame_type->SetUINT32(MF_MT_DEFAULT_STRIDE, kWidth * 4);
    if (SUCCEEDED(hr)) hr = frame_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (SUCCEEDED(hr)) *type = frame_type.Detach();
    return hr;
}

HRESULT CreateFrame(LONGLONG index, IMFSample** sample) {
    constexpr DWORD kSize = kWidth * kHeight * 4;
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(kSize, &buffer);
    BYTE* data = nullptr;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        for (DWORD i = 0; i < kSize; i += 4) std::memcpy(data + i, kPixel, 4);
        buffer->Unlock();
        hr = buffer->SetCurrentLength(kSize);
    }
    ComPtr<IMFSample> frame;
    if (SUCCEEDED(hr)) hr = MFCreateSample(&frame);
    if (SUCCEEDED(hr)) hr = frame->AddBuffer(buffer.Get());
    if (SUCCEEDED(hr)) hr = frame->SetSampleTime(index * kFrameDuration);
    if (SUCCEEDED(hr)) hr = frame->SetSampleDuration(kFrameDuration);
    if (SUCCEEDED(hr)) *sample = frame.Detach();
    return hr;
}

// The camera's only stream. With `deliver` off it never answers a sample request, like a camera
// that has no frame to give or stopped giving them when it was shut down.
class FakeStream
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, ChainInterfaces<IMFMediaStream, IMFMediaEventGenerator>> {
  public:
    HRESULT RuntimeClassInitialize(IMFMediaSource* source, IMFStreamDescriptor* descriptor, bool deliver) {
        source_ = source;
        descriptor_ = descriptor;
        deliver_ = deliver;
        return MFCreateEventQueue(&queue_);
    }

    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override { return queue_->GetEvent(flags, event); }
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override {
        return queue_->BeginGetEvent(callback, state);
    }
    STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override {
        return queue_->EndGetEvent(result, event);
    }
    STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extended, HRESULT status, const PROPVARIANT* value) override {
        return queue_->QueueEventParamVar(type, extended, status, value);
    }

    STDMETHODIMP GetMediaSource(IMFMediaSource** source) override {
        std::lock_guard lock(mutex_);
        return source_ ? source_.CopyTo(source) : MF_E_SHUTDOWN;
    }
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override {
        std::lock_guard lock(mutex_);
        return source_ ? descriptor_.CopyTo(descriptor) : MF_E_SHUTDOWN;
    }
    STDMETHODIMP RequestSample(IUnknown* token) override {
        std::lock_guard lock(mutex_);
        if (!source_) return MF_E_SHUTDOWN;
        if (!deliver_) return S_OK;  // The request stays pending
        ComPtr<IMFSample> sample;
        HRESULT hr = CreateFrame(frames_++, &sample);
        if (SUCCEEDED(hr) && token) hr = sample->SetUnknown(MFSampleExtension_Token, token);
        if (SUCCEEDED(hr)) hr = queue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
        return hr;
    }

    void Shutdown() {
        std::lock_guard lock(mutex_);
        source_.Reset();  // Breaks the source <-> stream reference cycle
        queue_->Shutdown();
    }

  private:
    std::mutex mutex_;
    ComPtr<IMFMediaEventQueue> queue_;
    ComPtr<IMFMediaSource> source_;
    ComPtr<IMFStreamDescriptor> descriptor_;
    bool deliver_{false};
    LONGLONG frames_{0};
};

// A live RGB32 camera of kWidth x kHeight, shut down the way real media sources are: its event
// queues stop.
class FakeCamera
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, ChainInterfaces<IMFMediaSource, IMFMediaEventGenerator>> {
  public:
    HRESULT RuntimeClassInitialize(bool deliver) {
        ComPtr<IMFMediaType> type;
        ComPtr<IMFStreamDescriptor> stream_descriptor;
        ComPtr<IMFMediaTypeHandler> handler;
        HRESULT hr = MFCreateEventQueue(&queue_);
        if (SUCCEEDED(hr)) hr = CreateFrameType(&type);
        if (SUCCEEDED(hr)) hr = MFCreateStreamDescriptor(0, 1, type.GetAddressOf(), &stream_descriptor);
        if (SUCCEEDED(hr)) hr = stream_descriptor->GetMediaTypeHandler(&handler);
        if (SUCCEEDED(hr)) hr = handler->SetCurrentMediaType(type.Get());
        if (SUCCEEDED(hr)) hr = MFCreatePresentationDescriptor(1, stream_descriptor.GetAddressOf(), &descriptor_);
        if (SUCCEEDED(hr)) hr = descriptor_->SelectStream(0);
        if (SUCCEEDED(hr)) hr = MakeAndInitialize<FakeStream>(&stream_, this, stream_descriptor.Get(), deliver);
        return hr;
    }

    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override { return queue_->GetEvent(flags, event); }
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override {
        return queue_->BeginGetEvent(callback, state);
    }
    STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override {
        return queue_->EndGetEvent(result, event);
    }
    STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extended, HRESULT status, const PROPVARIANT* value) override {
        return queue_->QueueEventParamVar(type, extended, status, value);
    }

    STDMETHODIMP GetCharacteristics(DWORD* characteristics) override {
        std::lock_guard lock(mutex_);
        if (shut_down_) return MF_E_SHUTDOWN;
        *characteristics = MFMEDIASOURCE_IS_LIVE;
        return S_OK;
    }
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) override {
        std::lock_guard lock(mutex_);
        return shut_down_ ? MF_E_SHUTDOWN : descriptor_->Clone(descriptor);
    }
    STDMETHODIMP Start(IMFPresentationDescriptor*, const GUID*, const PROPVARIANT*) override {
        std::lock_guard lock(mutex_);
        if (shut_down_) return MF_E_SHUTDOWN;
        PROPVARIANT empty;
        PropVariantInit(&empty);
        IMFMediaStream* stream = stream_.Get();
        HRESULT hr = queue_->QueueEventParamUnk(started_ ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK, stream);
        if (SUCCEEDED(hr)) hr = stream_->QueueEvent(MEStreamStarted, GUID_NULL, S_OK, &empty);
        if (SUCCEEDED(hr)) hr = queue_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &empty);
        started_ = started_ || SUCCEEDED(hr);
        return hr;
    }
    STDMETHODIMP Stop() override {
        std::lock_guard lock(mutex_);
        if (shut_down_) return MF_E_SHUTDOWN;
        PROPVARIANT empty;
        PropVariantInit(&empty);
        HRESULT hr = stream_->QueueEvent(MEStreamStopped, GUID_NULL, S_OK, &empty);
        if (SUCCEEDED(hr)) hr = queue_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, &empty);
        return hr;
    }
    STDMETHODIMP Pause() override { return MF_E_INVALID_STATE_TRANSITION; }
    STDMETHODIMP Shutdown() override {
        std::lock_guard lock(mutex_);
        if (shut_down_) return MF_E_SHUTDOWN;
        shut_down_ = true;
        stream_->Shutdown();
        return queue_->Shutdown();
    }

  private:
    std::mutex mutex_;
    ComPtr<IMFMediaEventQueue> queue_;
    ComPtr<IMFPresentationDescriptor> descriptor_;
    ComPtr<FakeStream> stream_;
    bool started_{false};
    bool shut_down_{false};
};

std::unique_ptr<CameraSource> FakeCameraSource(bool deliver) {
    return win::CreateMfCameraSource([deliver](std::string* name) {
        *name = "fake camera";
        ComPtr<FakeCamera> camera;
        ComPtr<IMFMediaSource> source;
        if (SUCCEEDED(MakeAndInitialize<FakeCamera>(&camera, deliver))) source = camera;
        return source;
    });
}

// Open, one Read and Close on a thread of their own, as on a capture thread. A run that hangs is
// abandoned (detached, its state leaked) so the test fails instead of hanging.
class CaptureThread {
  public:
    explicit CaptureThread(std::unique_ptr<CameraSource> source) : run_(std::make_shared<Run>()) {
        run_->source = std::move(source);
        opened_ = run_->opened.get_future().share();
        read_ = run_->read.get_future().share();
        closed_ = run_->closed.get_future().share();
        thread_ = std::thread([run = run_] { run->Execute(); });
    }
    ~CaptureThread() {
        if (closed_.wait_for(kHangTimeout) == std::future_status::ready) {
            thread_.join();
        } else {
            thread_.detach();
        }
    }

    CameraSource& source() { return *run_->source; }
    bool Opened() const { return opened_.wait_for(kHangTimeout) == std::future_status::ready && opened_.get(); }
    /// What Read() returned, or nullopt while it is still waiting after `timeout`.
    std::optional<ReadStatus> ReadResult(std::chrono::milliseconds timeout) const {
        if (read_.wait_for(timeout) != std::future_status::ready) return std::nullopt;
        return read_.get();
    }
    bool Closed(std::chrono::milliseconds timeout) const {
        return closed_.wait_for(timeout) == std::future_status::ready;
    }
    /// The frame Read() wrote; valid once ReadResult() has a value.
    const std::vector<uint8_t>& frame() const { return run_->frame; }

  private:
    struct Run {
        std::unique_ptr<CameraSource> source;
        std::vector<uint8_t> frame;
        std::promise<bool> opened;
        std::promise<ReadStatus> read;
        std::promise<void> closed;

        void Execute() {
            const bool ok = source->Open();
            opened.set_value(ok);
            if (ok) {
                frame.resize(static_cast<size_t>(source->Width()) * source->Height() * 4);
                read.set_value(source->Read(frame.data(), source->Width() * 4));
            } else {
                read.set_value(ReadStatus::kFailed);
            }
            source->Close();
            closed.set_value();
        }
    };

    std::shared_ptr<Run> run_;
    std::shared_future<bool> opened_;
    std::shared_future<ReadStatus> read_;
    std::shared_future<void> closed_;
    std::thread thread_;
};

TEST(MfCameraSourceTest, ReadsTheFramesTheCameraDelivers) {
    CaptureThread capture(FakeCameraSource(/*deliver=*/true));
    ASSERT_TRUE(capture.Opened());
    EXPECT_EQ(capture.source().Width(), static_cast<int>(kWidth));
    EXPECT_EQ(capture.source().Height(), static_cast<int>(kHeight));
    ASSERT_EQ(capture.ReadResult(kHangTimeout), ReadStatus::kFrame);
    const std::vector<uint8_t>& frame = capture.frame();
    EXPECT_TRUE(std::equal(kPixel, kPixel + 3, frame.begin())) << "first pixel";
    EXPECT_TRUE(std::equal(kPixel, kPixel + 3, frame.end() - 4)) << "last pixel";
    EXPECT_TRUE(capture.Closed(kHangTimeout));
}

// Stopping the capture must not wait for a frame the camera will never deliver: closing the
// camera preview (switching pages, starting a recording, unticking the camera) hung the GUI here.
TEST(MfCameraSourceTest, InterruptEndsAReadThatWaitsForAFrame) {
    CaptureThread capture(FakeCameraSource(/*deliver=*/false));
    ASSERT_TRUE(capture.Opened());
    ASSERT_EQ(capture.ReadResult(300ms), std::nullopt) << "the fake camera delivers no frame";
    capture.source().Interrupt();
    EXPECT_EQ(capture.ReadResult(2s), ReadStatus::kNoFrame) << "Read() still waits after Interrupt()";
    EXPECT_TRUE(capture.Closed(2s)) << "Close() did not return";
}

}  // namespace
}  // namespace mvp::capture

#endif  // _WIN32
