#ifndef MVP_VIDEO_RENDERER_H_
#define MVP_VIDEO_RENDERER_H_

#include <cstdint>
#include <memory>
#include <string>

#include "media_frame.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SwsContext;

namespace mvp {

namespace gpu {
class FramePresenter;
class GpuDevice;
}  // namespace gpu

/// GPU-accelerated video renderer using SDL3.
/// Creates an SDL renderer attached to a parent window (embedded mode).
/// Software frames are uploaded as YUV textures; hardware frames are bound
/// directly to the backend device (zero-copy) when the backend supports it.
/// Every texture is tagged with the frame's colorimetry (matrix, range,
/// primaries, transfer); SDL's defaults would show limited-range BT.709
/// video as full-range BT.601 and SDR 10-bit video as HDR10.
/// Owns the GPU device it presents with and shares it with the graph.
///
/// Threading: Open/Close/RecoverDevice/SetZeroCopy on the control thread
/// while no frame is rendered; everything else on the render thread.
class VideoRenderer {
  public:
    VideoRenderer();
    ~VideoRenderer();

    VideoRenderer(const VideoRenderer&) = delete;
    VideoRenderer& operator=(const VideoRenderer&) = delete;

    /// Initialize renderer with a native window handle (HWND on Windows).
    bool Open(void* native_window_handle, int width, int height);

    /// Shut down SDL renderer and release all resources.
    void Close();

    /// Render a video frame. Dispatches to hw/nv12/yuv420p/fallback paths.
    void Render(const MediaFrame& frame);

    /// Notify that the parent window was resized. Only a fallback: the
    /// letterbox is computed from SDL's real output size in pixels, so it
    /// stays right under display scaling (Qt reports logical sizes).
    void Resize(int width, int height);

    bool IsOpen() const { return renderer_ != nullptr; }

    /// True once presentation failed because SDL had to recreate the GPU
    /// device (removal/reset). The graph still holds the old device, so it
    /// must be rebuilt after RecoverDevice().
    bool DeviceLost() const { return device_lost_; }

    /// Drops everything derived from the old GPU device and wraps the one
    /// SDL recreated, so a rebuilt graph can share it again.
    void RecoverDevice();

    /// Debug switch: when off, hardware frames are downloaded and uploaded
    /// like software frames instead of being bound zero-copy.
    void SetZeroCopy(bool enabled) { zero_copy_ = enabled; }

    /// SDL render backend in use, e.g. "direct3d11".
    const std::string& BackendName() const { return backend_name_; }

    /// Hardware frame domain this renderer can present zero-copy (kUnknown
    /// when the backend has no shareable GPU device). Valid after Open();
    /// read by the sink during DeclareCaps.
    PixelFormat BindableHardwareDomain() const { return bindable_domain_; }

    /// The GPU device behind the renderer backend, shared with the graph so
    /// decode, GPU effects and presentation use one device. nullptr when the
    /// backend has none. Set by Open(), released by Close().
    std::shared_ptr<gpu::GpuDevice> SharedGpuDevice() const { return gpu_device_; }

  private:
    // Open helpers
    bool CreateRenderer();
    void ProbeBackend();

    void RenderSoftwareFrame(const MediaFrame& frame);
    // Software path: YUV420P direct upload
    void RenderYUV420P(const MediaFrame& frame);
    // Software path: format conversion fallback
    void RenderFallback(const MediaFrame& frame);
    // NV12 direct upload
    void RenderNV12(const MediaFrame& frame);
    // Hardware frame: bind through the presenter, download when unbindable
    void RenderHWFrame(const MediaFrame& frame);
    bool RenderBoundTexture(const MediaFrame& frame, void* texture);

    void Present(int frame_width, int frame_height);
    void Present(SDL_Texture* texture, int frame_width, int frame_height);
    void CheckPresented(bool presented);
    void EnsureTexture(int frame_width, int frame_height, int sdl_format, uint32_t colorspace);
    void RenderClear();
    void ComputeDestRect(int frame_width, int frame_height,
                         float* x, float* y, float* w, float* h) const;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;

    int texture_width_ = 0;
    int texture_height_ = 0;
    int texture_format_ = 0;
    uint32_t texture_colorspace_ = 0;  // SDL_Colorspace the texture was created with
    int window_width_ = 0;
    int window_height_ = 0;

    PixelFormat bindable_domain_{PixelFormat::kUnknown};
    // Shared with the graph; null = software pipeline, no context to serialize.
    std::shared_ptr<gpu::GpuDevice> gpu_device_;
    std::unique_ptr<gpu::FramePresenter> presenter_;
    // The SDL D3D11 device wrapped into gpu_device_; SDL swaps it on reset.
    void* native_device_ = nullptr;
    bool device_lost_ = false;
    bool present_failing_ = false;  // Logs a present failure once per streak
    bool zero_copy_ = true;
    std::string backend_name_;

    // Fallback conversion for frames without a direct upload path; cached
    // per source size/format.
    SwsContext* sws_ctx_ = nullptr;
    int sws_failed_format_ = -1;  // Last source format without a conversion
    uint8_t* convert_buffer_ = nullptr;
    int convert_buffer_size_ = 0;
};

}  // namespace mvp

#endif  // MVP_VIDEO_RENDERER_H_
