// Windows implementation of capture::AudioSource on WASAPI shared mode:
// loopback of an output device, or a capture device (microphone).
#include "capture/audio_source.h"

#include "capture/win/win_util.h"  // First: configures <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <algorithm>
#include <bitset>
#include <cstring>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}
#include <spdlog/spdlog.h>

#include "base/ffmpeg_utils.h"
#include "capture/win/audio_endpoints.h"

namespace mvp::capture {
namespace {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// Shared-mode buffer: the device keeps this much audio if the reader is late.
constexpr REFERENCE_TIME kBufferDuration = 10'000'000;  // 1 s in 100-ns units

class WasapiSource : public AudioSource {
  public:
    WasapiSource(AudioDeviceKind kind, std::string device_id)
        : kind_(kind), device_id_(std::move(device_id)) {}
    ~WasapiSource() override { Close(); }

    bool Open() override;
    void Close() override;
    int SampleRate() const override { return rate_; }
    int Channels() const override { return channels_; }
    int SampleFormat() const override { return format_; }
    std::string Description() const override;
    bool Read(const PacketSink& sink) override;

  private:
    bool OpenDevice();
    bool StartClient();
    bool ReadFormat(const WAVEFORMATEX* format);
    bool FillFrame(const BYTE* data, UINT32 frames, DWORD flags, AVFrame* frame) const;
    bool Failed(const char* what, HRESULT hr);

    const AudioDeviceKind kind_;
    const std::string device_id_;
    std::unique_ptr<win::ScopedCom> com_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> capture_;
    std::string name_;
    int rate_{0};
    int channels_{0};
    int block_align_{0};
    int format_{AV_SAMPLE_FMT_NONE};
    uint64_t channel_mask_{0};
};

bool WasapiSource::Open() {
    Close();
    com_ = std::make_unique<win::ScopedCom>();
    if (!com_->Ok()) {
        SPDLOG_ERROR("WasapiSource: COM unavailable on the capture thread");
        return false;
    }
    if (!OpenDevice() || !StartClient()) {
        Close();
        return false;
    }
    SPDLOG_INFO("WasapiSource: {}", Description());
    return true;
}

bool WasapiSource::OpenDevice() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return Failed("creating the device enumerator", hr);
    if (device_id_.empty()) {
        const EDataFlow flow = kind_ == AudioDeviceKind::kOutput ? eRender : eCapture;
        hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device_);
    } else {
        hr = enumerator->GetDevice(win::ToWide(device_id_).c_str(), &device_);
    }
    if (FAILED(hr)) return Failed("finding the device", hr);
    name_ = win::EndpointName(device_.Get());
    return true;
}

bool WasapiSource::StartClient() {
    HRESULT hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(client_.GetAddressOf()));
    if (FAILED(hr)) return Failed("activating the audio client", hr);
    WAVEFORMATEX* mix = nullptr;
    hr = client_->GetMixFormat(&mix);
    if (FAILED(hr)) return Failed("reading the mix format", hr);
    const bool supported = ReadFormat(mix);
    // Loopback records what the output device plays ("what you hear").
    const DWORD flags = kind_ == AudioDeviceKind::kOutput ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    hr = supported ? client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration, 0, mix, nullptr)
                   : E_INVALIDARG;
    CoTaskMemFree(mix);
    if (FAILED(hr)) return Failed("initializing the audio client", hr);
    hr = client_->GetService(IID_PPV_ARGS(&capture_));
    if (FAILED(hr)) return Failed("getting the capture client", hr);
    hr = client_->Start();
    return SUCCEEDED(hr) || Failed("starting the stream", hr);
}

bool WasapiSource::ReadFormat(const WAVEFORMATEX* format) {
    rate_ = static_cast<int>(format->nSamplesPerSec);
    channels_ = format->nChannels;
    block_align_ = format->nBlockAlign;
    WORD tag = format->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        tag = static_cast<WORD>(ext->SubFormat.Data1);  // The sub-format GUID embeds the tag
        channel_mask_ = ext->dwChannelMask;
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 32) {
        format_ = AV_SAMPLE_FMT_FLT;
    } else if (tag == WAVE_FORMAT_PCM && format->wBitsPerSample == 16) {
        format_ = AV_SAMPLE_FMT_S16;
    } else if (tag == WAVE_FORMAT_PCM && format->wBitsPerSample == 32) {
        format_ = AV_SAMPLE_FMT_S32;
    } else {
        SPDLOG_ERROR("WasapiSource: unsupported mix format (tag {}, {} bits)", tag, format->wBitsPerSample);
        return false;
    }
    return true;
}

void WasapiSource::Close() {
    if (client_) client_->Stop();
    capture_.Reset();
    client_.Reset();
    device_.Reset();
    com_.reset();
}

std::string WasapiSource::Description() const {
    const char* role = kind_ == AudioDeviceKind::kOutput ? "loopback of" : "microphone";
    const char* format = av_get_sample_fmt_name(static_cast<AVSampleFormat>(format_));
    return std::string(role) + " '" + name_ + "', " + std::to_string(rate_) + " Hz, " +
           std::to_string(channels_) + " ch, " + (format ? format : "?");
}

bool WasapiSource::Read(const PacketSink& sink) {
    for (;;) {
        UINT32 pending = 0;
        HRESULT hr = capture_->GetNextPacketSize(&pending);
        if (FAILED(hr)) return Failed("polling the stream", hr);
        if (pending == 0) return true;
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        UINT64 qpc_position = 0;
        hr = capture_->GetBuffer(&data, &frames, &flags, nullptr, &qpc_position);
        if (FAILED(hr)) return Failed("reading a packet", hr);
        if (frames > 0) {
            AVFramePtr frame;
            // The device timestamps its first sample on the QPC timeline.
            const int64_t age = qpc_position ? win::QpcNow100ns() - static_cast<int64_t>(qpc_position) : 0;
            if (FillFrame(data, frames, flags, frame.get())) sink(frame.get(), std::clamp(age / 1e7, 0.0, 1.0));
        }
        capture_->ReleaseBuffer(frames);
    }
}

bool WasapiSource::FillFrame(const BYTE* data, UINT32 frames, DWORD flags, AVFrame* frame) const {
    frame->format = format_;
    frame->sample_rate = rate_;
    frame->nb_samples = static_cast<int>(frames);
    const bool mask_matches = std::bitset<64>(channel_mask_).count() == static_cast<size_t>(channels_);
    if (!mask_matches || av_channel_layout_from_mask(&frame->ch_layout, channel_mask_) < 0) {
        av_channel_layout_default(&frame->ch_layout, channels_);
    }
    if (av_frame_get_buffer(frame, 0) < 0) {
        return false;
    }
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        av_samples_set_silence(frame->data, 0, frame->nb_samples, channels_,
                               static_cast<AVSampleFormat>(format_));
    } else {
        std::memcpy(frame->data[0], data, static_cast<size_t>(frames) * block_align_);
    }
    return true;
}

bool WasapiSource::Failed(const char* what, HRESULT hr) {
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
        SPDLOG_WARN("WasapiSource: '{}' went away while {}", name_, what);
    } else {
        SPDLOG_ERROR("WasapiSource: {} failed ({})", what, win::HresultString(hr));
    }
    return false;
}

}  // namespace

std::unique_ptr<AudioSource> CreateAudioSource(AudioDeviceKind kind, const std::string& device_id) {
    return std::make_unique<WasapiSource>(kind, device_id);
}

}  // namespace mvp::capture
