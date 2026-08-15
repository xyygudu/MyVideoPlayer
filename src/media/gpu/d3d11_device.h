#ifndef MVP_GPU_D3D11_DEVICE_H_
#define MVP_GPU_D3D11_DEVICE_H_

#include "gpu/gpu_device.h"

namespace mvp::gpu {

/// D3D11VA backend: the only place in the codebase that mentions D3D11.
class D3D11GpuDevice final : public GpuDevice {
  public:
    /// Wrap an externally owned ID3D11Device (SDL3 renderer backend).
    /// FFmpeg takes ownership of the interface, so the wrapper must be
    /// destroyed before the external owner releases its own reference.
    static std::unique_ptr<GpuDevice> Wrap(void* native_device);

    ~D3D11GpuDevice() override;

    PixelFormat Domain() const override { return PixelFormat::kD3D11; }
    AVBufferRef* DeviceRef() const override { return device_ref_; }
    bool SupportsDecoder(const AVCodec* codec) const override;

  private:
    explicit D3D11GpuDevice(AVBufferRef* device_ref);

    AVBufferRef* device_ref_{nullptr};
};

}  // namespace mvp::gpu

#endif  // MVP_GPU_D3D11_DEVICE_H_
