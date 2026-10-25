#include "capture/win/audio_endpoints.h"

#include <propsys.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

#include "capture/win/win_util.h"

namespace mvp::capture::win {
namespace {

using Microsoft::WRL::ComPtr;

// PKEY_Device_FriendlyName, defined here so no translation unit needs INITGUID.
constexpr PROPERTYKEY kFriendlyNameKey = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

EDataFlow ToDataFlow(AudioDeviceKind kind) { return kind == AudioDeviceKind::kOutput ? eRender : eCapture; }

std::string DefaultEndpointId(IMMDeviceEnumerator* enumerator, EDataFlow flow) {
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device))) return {};
    return EndpointId(device.Get());
}

}  // namespace

std::string EndpointName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    std::string name;
    if (SUCCEEDED(store->GetValue(kFriendlyNameKey, &value)) && value.vt == VT_LPWSTR) {
        name = ToUtf8(value.pwszVal);
    }
    PropVariantClear(&value);
    return name;
}

std::string EndpointId(IMMDevice* device) {
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id))) return {};
    std::string text = ToUtf8(id);
    CoTaskMemFree(id);
    return text;
}

std::vector<AudioDeviceInfo> EnumerateAudioEndpoints(AudioDeviceKind kind) {
    std::vector<AudioDeviceInfo> result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    ComPtr<IMMDeviceCollection> devices;
    if (SUCCEEDED(hr)) hr = enumerator->EnumAudioEndpoints(ToDataFlow(kind), DEVICE_STATE_ACTIVE, &devices);
    if (FAILED(hr)) {
        SPDLOG_WARN("Audio endpoints: enumeration failed ({})", HresultString(hr));
        return result;
    }
    const std::string default_id = DefaultEndpointId(enumerator.Get(), ToDataFlow(kind));
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(i, &device))) continue;
        AudioDeviceInfo info;
        info.id = EndpointId(device.Get());
        info.name = EndpointName(device.Get());
        info.is_default = !info.id.empty() && info.id == default_id;
        if (!info.id.empty()) result.push_back(std::move(info));
    }
    return result;
}

}  // namespace mvp::capture::win
