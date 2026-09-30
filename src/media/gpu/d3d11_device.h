#ifndef MVP_GPU_D3D11_DEVICE_H_
#define MVP_GPU_D3D11_DEVICE_H_

#include <memory>

#include "gpu/gpu_device.h"

namespace mvp::gpu {

/// D3D11VA backend. D3D11 types stay inside the gpu/d3d11_* sources.
class D3D11GpuDevice final : public GpuDevice {
  public:
    static std::shared_ptr<GpuDevice> Wrap(void* native_device);

    ~D3D11GpuDevice() override;

    PixelFormat Domain() const override { return PixelFormat::kD3D11; }
    AVBufferRef* DeviceRef() const override { return device_ref_; }
    bool SupportsDecoder(const AVCodec* codec) const override;
    void RefineDecoderFrames(AVHWFramesContext* frames) const override;
    void LockContext() override;
    void UnlockContext() override;
    std::unique_ptr<VideoPass> CreateVideoPass(VideoKernel kernel) override;
    std::unique_ptr<FramePresenter> CreatePresenter() override;

  private:
    D3D11GpuDevice(AVBufferRef* device_ref, bool video_passes_supported);

    AVBufferRef* device_ref_{nullptr};
    // Passes render into NV12 planes; not every GPU exposes planar targets.
    bool video_passes_supported_{false};
};

}  // namespace mvp::gpu

#endif  // MVP_GPU_D3D11_DEVICE_H_
