#include "video_pass_common.hlsli"

// Mirrors gpu::ColorAdjustUniforms.
cbuffer ColorAdjust : register(b1) {
    float brightness;
    float contrast;
    float saturation;
    float color_padding;
};

// Same formulas as pixel_ops::BuildColorLut, in normalized code values; the
// UNORM render target rounds to nearest like the CPU LUT does.
float PSLuma(VsOutput input) : SV_Target {
    float y = luma_plane.Load(int4(int2(input.position.xy), 0, 0));
    return saturate((y - neutral) * contrast + neutral + brightness);
}

float2 PSChroma(VsOutput input) : SV_Target {
    float2 c = chroma_plane.Load(int4(int2(input.position.xy), 0, 0));
    return saturate((c - neutral) * saturation + neutral);
}
