#include "pixel_ops.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "nodes/transform_effect_node.h"

namespace mvp::pixel_ops {

namespace {

constexpr float kPi = 3.14159265358979323846f;

inline uint8_t ApplyLinear(uint8_t v, float scale, float offset) {
    float r = (static_cast<float>(v) - 128.0f) * scale + 128.0f + offset;
    return static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f) + 0.5f);
}

void InverseMap(const AffineMapping& m, float dst_x, float dst_y, float* src_x, float* src_y) {
    float ux = dst_x - m.cx - m.tx;
    float uy = dst_y - m.cy - m.ty;
    *src_x = m.inv[0] * ux + m.inv[1] * uy + m.cx;
    *src_y = m.inv[2] * ux + m.inv[3] * uy + m.cy;
}

uint8_t SampleBilinear(const uint8_t* plane, int linesize, int width, int height,
                       int comp_stride, int comp_offset, float x, float y, uint8_t fill) {
    auto texel = [&](int ix, int iy) -> float {
        if (ix < 0 || ix >= width || iy < 0 || iy >= height) return static_cast<float>(fill);
        return static_cast<float>(
            plane[static_cast<ptrdiff_t>(iy) * linesize + ix * comp_stride + comp_offset]);
    };
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(x0);
    float fy = y - static_cast<float>(y0);
    float p00 = texel(x0, y0);
    float p10 = texel(x0 + 1, y0);
    float p01 = texel(x0, y0 + 1);
    float p11 = texel(x0 + 1, y0 + 1);
    float v = (1.0f - fx) * (1.0f - fy) * p00 + fx * (1.0f - fy) * p10 +
              (1.0f - fx) * fy * p01 + fx * fy * p11;
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f);
}

int QuantizeRotation(float deg) {
    int d = static_cast<int>(std::round(deg)) % 360;
    if (d < 0) d += 360;
    if (d <= 45 || d >= 315) return 0;
    if (d >= 135 && d <= 225) return 180;
    if (d > 45 && d < 135) return 90;
    return 270;
}

void PermuteCopy(const uint8_t* src, int src_ls, uint8_t* dst, int dst_ls,
                 int w, int h, int comp_stride, int rot, bool flip_h, bool flip_v) {
    for (int y = 0; y < h; ++y) {
        uint8_t* d = dst + y * dst_ls;
        for (int x = 0; x < w; ++x) {
            int sx = x, sy = y;
            switch (rot) {
                case 90:  sx = h - 1 - y; sy = x; break;
                case 180: sx = w - 1 - x; sy = h - 1 - y; break;
                case 270: sx = y; sy = w - 1 - x; break;
            }
            if (flip_h) sx = w - 1 - sx;
            if (flip_v) sy = h - 1 - sy;
            d[x * comp_stride] = src[sy * src_ls + sx * comp_stride];
        }
    }
}

}  // namespace

ColorLut BuildColorLut(float brightness, float contrast, float saturation) {
    ColorLut lut;
    float b_off = brightness * 255.0f;
    for (int i = 0; i < 256; ++i) {
        lut.y[i] = ApplyLinear(static_cast<uint8_t>(i), contrast, b_off);
        lut.uv[i] = ApplyLinear(static_cast<uint8_t>(i), saturation, 0.0f);
    }
    return lut;
}

void ApplyLut(uint8_t* plane, int linesize, int width, int height, const uint8_t lut[256]) {
    for (int y = 0; y < height; ++y) {
        uint8_t* row = plane + y * linesize;
        for (int x = 0; x < width; ++x) row[x] = lut[row[x]];
    }
}

AffineMapping ComputeAffineMapping(const graph::TransformAffineParams& p, int plane_w, int plane_h) {
    AffineMapping m;
    m.cx = plane_w * 0.5f;
    m.cy = plane_h * 0.5f;
    m.tx = p.translate_x * plane_w;
    m.ty = p.translate_y * plane_h;

    float cos_t = std::cos(p.rotate_rad);
    float sin_t = std::sin(p.rotate_rad);
    float sx = (p.scale_x != 0.0f) ? 1.0f / p.scale_x : 1.0f;
    float sy = (p.scale_y != 0.0f) ? 1.0f / p.scale_y : 1.0f;
    if (p.flip_h) sx = -sx;
    if (p.flip_v) sy = -sy;

    m.inv[0] = sx * cos_t;
    m.inv[1] = sx * sin_t * (p.flip_h ? -1.0f : 1.0f);
    m.inv[2] = -sy * sin_t * (p.flip_v ? -1.0f : 1.0f);
    m.inv[3] = sy * cos_t;
    return m;
}

void RemapPlane(const uint8_t* src, int src_linesize, uint8_t* dst, int dst_linesize,
                int width, int height, int comp_stride, int comp_offset,
                uint8_t fill, const AffineMapping& m) {
    for (int y = 0; y < height; ++y) {
        uint8_t* d = dst + static_cast<ptrdiff_t>(y) * dst_linesize;
        for (int x = 0; x < width; ++x) {
            float sx, sy;
            InverseMap(m, static_cast<float>(x), static_cast<float>(y), &sx, &sy);
            d[x * comp_stride + comp_offset] =
                SampleBilinear(src, src_linesize, width, height, comp_stride, comp_offset, sx, sy, fill);
        }
    }
}

void RemapInterleavedPlane(const uint8_t* src, int src_linesize,
                           uint8_t* dst, int dst_linesize,
                           int width, int height, uint8_t fill, const AffineMapping& m) {
    for (int y = 0; y < height; ++y) {
        uint8_t* d = dst + static_cast<ptrdiff_t>(y) * dst_linesize;
        for (int x = 0; x < width; ++x) {
            float sx, sy;
            InverseMap(m, static_cast<float>(x), static_cast<float>(y), &sx, &sy);
            d[x * 2]     = SampleBilinear(src, src_linesize, width, height, 2, 0, sx, sy, fill);
            d[x * 2 + 1] = SampleBilinear(src, src_linesize, width, height, 2, 1, sx, sy, fill);
        }
    }
}

bool TryPermutePlane(const uint8_t* src, int src_linesize, uint8_t* dst, int dst_linesize,
                     int width, int height, int comp_stride, int comp_offset,
                     const graph::TransformAffineParams& p) {
    if (p.scale_x != 1.0f || p.scale_y != 1.0f) return false;
    if (p.translate_x != 0.0f || p.translate_y != 0.0f) return false;
    int rot = QuantizeRotation(p.rotate_rad * 180.0f / kPi);

    if (rot == 0 && !p.flip_h && !p.flip_v && comp_offset == 0) {
        int row_bytes = width * comp_stride;
        for (int y = 0; y < height; ++y)
            std::memcpy(dst + y * dst_linesize, src + y * src_linesize, row_bytes);
        return true;
    }

    if (comp_offset != 0) return false;
    if ((rot == 90 || rot == 270) && width != height) return false;  // 90°/270° requires square plane

    PermuteCopy(src, src_linesize, dst, dst_linesize, width, height, comp_stride,
                rot, p.flip_h, p.flip_v);
    return true;
}

}  // namespace mvp::pixel_ops
