#ifndef MVP_VIDEO_RENDERER_H_
#define MVP_VIDEO_RENDERER_H_

#include "media_frame.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SwsContext;

namespace mvp {

/// GPU-accelerated video renderer using SDL3.
/// Creates an SDL renderer attached to a parent window (embedded mode)
/// and uploads YUV textures for hardware-composited display.
/// Supports zero-copy rendering of D3D11VA hardware frames.
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

  private:
    // Software path: YUV420P direct upload
    void RenderYUV420P(const MediaFrame& frame);
    // Software path: format conversion fallback
    void RenderFallback(const MediaFrame& frame);
    // NV12 direct upload (used for hw_transfer or native NV12)
    void RenderNV12(const MediaFrame& frame);
    // D3D11VA zero-copy path
    void RenderHWFrame(const MediaFrame& frame);

    void Present(int frame_width, int frame_height);
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

    // Fallback conversion for non-YUV420P frames
    SwsContext* sws_ctx_ = nullptr;
    uint8_t* convert_buffer_ = nullptr;
    int convert_buffer_size_ = 0;
};

}  // namespace mvp

#endif  // MVP_VIDEO_RENDERER_H_
