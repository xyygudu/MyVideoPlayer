#include "gpu/gpu_device.h"

#include <spdlog/spdlog.h>

#include "gpu/d3d11_device.h"

namespace mvp::gpu {

// Platform registry: each backend registers here as it lands. The factory is
// deliberately tiny so adding the next backend is one line, not a refactor.
std::unique_ptr<GpuDevice> GpuDevice::WrapExternal(void* native_device) {
#if defined(_WIN32)
    return D3D11GpuDevice::Wrap(native_device);
#else
    (void)native_device;
    SPDLOG_WARN("GpuDevice: no backend available on this platform");
    return nullptr;
#endif
}

}  // namespace mvp::gpu
