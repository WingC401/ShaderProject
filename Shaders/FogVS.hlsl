// ============================================================
// FogVS.hlsl
// Fullscreen-triangle vertex shader for the distance fog pass.
// Same technique as CloudVS: expands SV_VertexID into one
// screen-covering triangle, carries clip-space xy.
// Author: Chan WingChung
// ============================================================

struct VS_OUTPUT_FOG
{
    float4 svPosition : SV_POSITION;
    float2 clip : TEXCOORD0; // NDC xy, for world-position rebuild
};

VS_OUTPUT_FOG main(uint vertexId : SV_VertexID)
{
    VS_OUTPUT_FOG lOut;
    float2 lUv = float2((vertexId << 1) & 2, vertexId & 2);
    lOut.svPosition = float4(lUv * 2.0f - 1.0f, 1.0f, 1.0f);
    lOut.clip = lOut.svPosition.xy;
    return lOut;
}