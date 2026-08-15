#ifndef MVP_GPU_D3D11_DEVICE_H_
#define MVP_GPU_D3D11_DEVICE_H_

#include <memory>
#include <mutex>
#include <vector>

#include "gpu/gpu_device.h"

struct ID3D11DeviceContext;
struct ID3D11Texture2D;

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
    void* CopyForPresentation(const AVFrame* hw_frame) override;

  private:
    explicit D3D11GpuDevice(AVBufferRef* device_ref);

    // Returns a pool texture matching w/h/format, rebuilding the ring when
    // the parameters change. Caller holds copy_mutex_.
    ID3D11Texture2D* AcquirePoolTexture(int width, int height, int dxgi_format);

    AVBufferRef* device_ref_{nullptr};
    // The device's command context, used only on the decode thread
    // (serialized by copy_mutex_).
    ID3D11DeviceContext* device_context_{nullptr};
    std::mutex copy_mutex_;

    // Ring of individual shader-resource textures for presentation binding.
    // Sized larger than the max frames in flight so a texture is never
    // overwritten while the sink or a link still references it (same
    // pool-size argument as the decoder's surface pool).
    std::vector<ID3D11Texture2D*> pool_;
    size_t pool_next_{0};
    int pool_width_{0};
    int pool_height_{0};
    int pool_format_{0};
};

}  // namespace mvp::gpu

#endif  // MVP_GPU_D3D11_DEVICE_H_
