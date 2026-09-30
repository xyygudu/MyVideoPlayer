// Shared declarations for per-plane video passes (gpu/d3d11_video_pass.cc):
// one full-screen triangle per output plane; source planes are read through
// Texture2DArray views so decoder array slices need no copy.

cbuffer PassConstants : register(b0) {
    float2 plane_size;    // Visible size of the plane being written, pixels
    float2 texture_size;  // Source texture plane size (decoder surfaces are padded)
    float neutral;        // Mid-grey code value: chroma zero and luma pivot
    float3 pass_padding;
};

Texture2DArray<float> luma_plane : register(t0);
Texture2DArray<float2> chroma_plane : register(t1);
SamplerState linear_clamp : register(s0);

struct VsOutput {
    float4 position : SV_Position;
};
