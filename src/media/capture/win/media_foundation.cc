#include "capture/win/media_foundation.h"

#include <mfidl.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

namespace mvp::capture::win {
namespace {

std::string AllocatedString(IMFActivate* activate, const GUID& key) {
    WCHAR* text = nullptr;
    UINT32 length = 0;
    if (FAILED(activate->GetAllocatedString(key, &text, &length))) return {};
    std::string result = ToUtf8(text);
    CoTaskMemFree(text);
    return result;
}

}  // namespace

std::vector<CameraInfo> EnumerateCameras() {
    std::vector<CameraInfo> cameras;
    ScopedMediaFoundation mf;
    Microsoft::WRL::ComPtr<IMFAttributes> attributes;
    if (!mf.Ok() || FAILED(MFCreateAttributes(&attributes, 1))) return cameras;
    attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    const HRESULT hr = MFEnumDeviceSources(attributes.Get(), &devices, &count);
    if (FAILED(hr)) {
        SPDLOG_WARN("Cameras: enumeration failed ({})", HresultString(hr));
        return cameras;
    }
    for (UINT32 i = 0; i < count; ++i) {
        CameraInfo info;
        info.id = AllocatedString(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
        info.name = AllocatedString(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
        if (!info.id.empty()) cameras.push_back(std::move(info));
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return cameras;
}

}  // namespace mvp::capture::win
