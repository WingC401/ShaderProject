// ============================================================
// FogUpsamplePS.hlsl
// Upsample-composite pass: samples the half-res fog buffer with
// bilinear filtering and alpha-blends it over the back buffer.
// Author: Chan WingChung
// ============================================================

Texture2D fogHalfTex : register(t5);
SamplerState fogLinearSampler : register(s5);

struct VS_OUTPUT_FOG
{
    float4 svPosition : SV_POSITION;
    float2 clip : TEXCOORD0;
};

float4 main(VS_OUTPUT_FOG input) : SV_TARGET
{
    // --- NDC xy -> texture uv (y flips)
    float2 lUv = float2(input.clip.x * 0.5f + 0.5f, 0.5f - input.clip.y * 0.5f);
    return fogHalfTex.SampleLevel(fogLinearSampler, lUv, 0);
}