#include "video_pass_common.hlsli"

// Mirrors gpu::AffineRemapUniforms.
cbuffer AffineRemap : register(b1) {
    float4 inverse;    // Row-major 2x2: (m00, m01, m10, m11)
    float2 translate;  // Fraction of the plane size
    float2 affine_padding;
};

// Backward mapping in the plane's own pixel space, as on the CPU path
// (pixel_ops::ComputeAffineMapping): pixel centers sit on integer coordinates.
float2 SourcePixel(float2 destination) {
    float2 center = plane_size * 0.5;
    float2 offset = destination - center - translate * plane_size;
    return float2(dot(inverse.xy, offset), dot(inverse.zw, offset)) + center;
}

bool Outside(float2 source) {
    return any(source < -0.5) || any(source > plane_size - 0.5);
}

// Clamped to the visible area: padded surface rows must not bleed in.
float3 SampleCoord(float2 source) {
    return float3((clamp(source, 0.0, plane_size - 1.0) + 0.5) / texture_size, 0.0);
}

float PSLuma(VsOutput input) : SV_Target {
    float2 source = SourcePixel(input.position.xy - 0.5);
    if (Outside(source)) {
        return 0.0;
    }
    return luma_plane.SampleLevel(linear_clamp, SampleCoord(source), 0.0);
}

float2 PSChroma(VsOutput input) : SV_Target {
    float2 source = SourcePixel(input.position.xy - 0.5);
    if (Outside(source)) {
        return float2(neutral, neutral);
    }
    return chroma_plane.SampleLevel(linear_clamp, SampleCoord(source), 0.0);
}
