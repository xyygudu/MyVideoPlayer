#ifndef MVP_GPU_VIDEO_PASS_H_
#define MVP_GPU_VIDEO_PASS_H_

#include <cstddef>

#include "media_frame.h"

namespace mvp::gpu {

/// GPU kernels a backend implements for video effects. A kernel runs once
/// per plane of a two-plane YUV hardware frame (NV12/P010).
enum class VideoKernel {
    kColorAdjust,  // Uniforms: ColorAdjustUniforms
    kAffineRemap,  // Uniforms: AffineRemapUniforms
};

// Uniform layouts mirror the shader constant buffers (16-byte registers).
// Code values are normalized: 1.0 is the full range of the format.

/// Y' = (Y - mid) * contrast + mid + brightness; C' = (C - mid) * saturation + mid.
struct ColorAdjustUniforms {
    float brightness{0.0f};
    float contrast{1.0f};
    float saturation{1.0f};
    float padding{0.0f};
};
static_assert(sizeof(ColorAdjustUniforms) % 16 == 0, "cbuffer register alignment");

/// Backward mapping evaluated in each plane's own pixel space:
/// src = inverse * (dst - center - translate * plane_size) + center.
struct AffineRemapUniforms {
    float inverse[4]{1.0f, 0.0f, 0.0f, 1.0f};  // Row-major 2x2
    float translate[2]{0.0f, 0.0f};            // Fraction of the plane size
    float padding[2]{0.0f, 0.0f};
};
static_assert(sizeof(AffineRemapUniforms) % 16 == 0, "cbuffer register alignment");

/// One kernel bound to a GPU device. Owned by a single node and run on that
/// node's thread; takes the device context lock internally.
class VideoPass {
  public:
    virtual ~VideoPass() = default;

    /// Runs the kernel over every plane of hardware frame `src` into a new
    /// hardware frame of the same size and layout; `src` is not modified.
    /// `uniforms` must point to the struct matching this pass's kernel.
    /// Returns an invalid frame on failure.
    virtual MediaFrame Run(const MediaFrame& src, const void* uniforms, size_t size) = 0;
};

}  // namespace mvp::gpu

#endif  // MVP_GPU_VIDEO_PASS_H_
