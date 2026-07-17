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

}  // namespace pixel_ops
}  // namespace mvp

#endif  // MVP_PIXEL_OPS_H_
