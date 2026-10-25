#ifndef MVP_PIXEL_OPS_H_
#define MVP_PIXEL_OPS_H_

#include <cstdint>

namespace mvp {
namespace graph {
struct TransformAffineParams;
}
namespace pixel_ops {

struct ColorLut {
    uint8_t y[256];
    uint8_t uv[256];
};

ColorLut BuildColorLut(float brightness, float contrast, float saturation);
void ApplyLut(uint8_t* plane, int linesize, int width, int height, const uint8_t lut[256]);

struct AffineMapping {
    float inv[4];
    float cx, cy;
    float tx, ty;
};

AffineMapping ComputeAffineMapping(const graph::TransformAffineParams& p, int plane_w, int plane_h);

void RemapPlane(const uint8_t* src, int src_linesize, uint8_t* dst, int dst_linesize,
                int width, int height, int comp_stride, int comp_offset,
                uint8_t fill, const AffineMapping& m);

void RemapInterleavedPlane(const uint8_t* src, int src_linesize,
                           uint8_t* dst, int dst_linesize,
                           int width, int height, uint8_t fill, const AffineMapping& m);

bool TryPermutePlane(const uint8_t* src, int src_linesize, uint8_t* dst, int dst_linesize,
                     int width, int height, int comp_stride, int comp_offset,
                     const graph::TransformAffineParams& p);

// --- Packed BGRA images (screen capture, overlays) ---

/// A BGRA image view: `stride` bytes per row.
struct BgraImage {
    uint8_t* data{nullptr};
    int width{0};
    int height{0};
    int stride{0};
};

/// Copies a `width` x `height` BGRA image into `dst` with its top-left at
/// (x, y), clipped to `dst`; `mirror` flips it horizontally.
void BlitBgra(const uint8_t* src, int src_stride, int width, int height, const BgraImage& dst, int x,
              int y, bool mirror);

/// Draws a `thickness`-pixel outline of the rectangle in color `rgb`
/// (0xRRGGBB), clipped to `dst`.
void OutlineRectBgra(const BgraImage& dst, int x, int y, int width, int height, int thickness,
                     uint32_t rgb);

/// Blends a disc of color `rgb` (0xRRGGBB) at opacity `alpha` (0..1) centred
/// on (cx, cy), with a one-pixel antialiased edge, clipped to `dst`.
void BlendDiscBgra(const BgraImage& dst, int cx, int cy, int radius, uint32_t rgb, float alpha);

}  // namespace pixel_ops
}  // namespace mvp

#endif  // MVP_PIXEL_OPS_H_
