#include "video_pass_common.hlsli"

// Full-screen triangle from SV_VertexID: no vertex buffer or input layout.
VsOutput VSMain(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VsOutput output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
