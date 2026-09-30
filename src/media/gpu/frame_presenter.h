#ifndef MVP_GPU_FRAME_PRESENTER_H_
#define MVP_GPU_FRAME_PRESENTER_H_

struct AVFrame;

namespace mvp::gpu {

/// Adapts hardware frames to what the presentation backend can bind. Owned
/// by the renderer; called on the render thread with the device context lock
/// held, in the same critical section as the draw that consumes the result.
class FramePresenter {
  public:
    virtual ~FramePresenter() = default;

    /// A texture the backend can bind for `hw_frame`: the frame's own texture
    /// when directly bindable, otherwise a presenter-owned copy of its visible
    /// area (valid until the next call). nullptr for unsupported layouts.
    virtual void* BindableTexture(const AVFrame* hw_frame) = 0;
};

}  // namespace mvp::gpu

#endif  // MVP_GPU_FRAME_PRESENTER_H_
