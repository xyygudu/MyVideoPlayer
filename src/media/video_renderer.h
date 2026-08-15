#ifndef MVP_VIDEO_RENDERER_H_
#define MVP_VIDEO_RENDERER_H_

#include "media_frame.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SwsContext;

namespace mvp {

/// GPU-accelerated video renderer using SDL3.
/// Creates an SDL renderer attached to a parent window (embedded mode).
/// Software frames are uploaded as YUV textures; hardware frames are bound
/// directly to the backend device (zero-copy) when the backend supports it.
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

    /// Notify that the parent window was resized.
    void Resize(int width, int height);

    bool IsOpen() const { return renderer_ != nullptr; }

    /// Hardware frame domain this renderer can present zero-copy (kUnknown
    /// when the backend has no external-texture binding support). Valid
    /// after Open(); read by the sink during DeclareCaps.
    PixelFormat BindableHardwareDomain() const { return bindable_domain_; }

    /// The renderer backend's native device (ID3D11Device* on the D3D11
    /// backend), nullptr otherwise. Non-owning, valid while the renderer is
    /// open. The pipeline builder wraps it so decode and present share one
    /// device.
    void* NativeDevice() const { return native_device_; }

  private:
    // Software path: YUV420P direct upload
    void RenderYUV420P(const MediaFrame& frame);
    // Software path: format conversion fallback
    void RenderFallback(const MediaFrame& frame);
    // NV12 direct upload (used for hw_transfer or native NV12)
    void RenderNV12(const MediaFrame& frame);
    // Hardware frame: direct binding or GPU→CPU transfer fallback
    void RenderHWFrame(const MediaFrame& frame);
    bool RenderBoundHwFrame(const MediaFrame& frame);
    void RenderHwTransfer(const MediaFrame& frame);

    void Present(int frame_width, int frame_height);
    void Present(SDL_Texture* texture, int frame_width, int frame_height);
    void EnsureTexture(int frame_width, int frame_height, int sdl_format);
    void RenderClear();
    void ComputeDestRect(int frame_width, int frame_height,
                         float* x, float* y, float* w, float* h) const;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;

    int texture_width_ = 0;
    int texture_height_ = 0;
    int texture_format_ = 0;
    int window_width_ = 0;
    int window_height_ = 0;

    PixelFormat bindable_domain_{PixelFormat::kUnknown};
    void* native_device_{nullptr};

    // Fallback conversion for non-YUV420P frames
    SwsContext* sws_ctx_ = nullptr;
    uint8_t* convert_buffer_ = nullptr;
    int convert_buffer_size_ = 0;
};

}  // namespace mvp

#endif  // MVP_VIDEO_RENDERER_H_
