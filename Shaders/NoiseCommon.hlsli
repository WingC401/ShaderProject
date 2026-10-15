// ============================================================
// NoiseCommon.hlsli
// Shared 3D value noise + fbm, extracted from CloudPS so the
// cloud and fog passes sample the same noise field.
// Author: Chan WingChung
// ============================================================

float Hash(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float ValueNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f); // smoothstep interpolation

    // --- trilinear blend of the 8 corner hashes
    float n000 = Hash(i + float3(0, 0, 0));
    float n100 = Hash(i + float3(1, 0, 0));
    float n010 = Hash(i + float3(0, 1, 0));
    float n110 = Hash(i + float3(1, 1, 0));
    float n001 = Hash(i + float3(0, 0, 1));
    float n101 = Hash(i + float3(1, 0, 1));
    float n011 = Hash(i + float3(0, 1, 1));
    float n111 = Hash(i + float3(1, 1, 1));

    float nx00 = lerp(n000, n100, f.x);
    float nx10 = lerp(n010, n110, f.x);
    float nx01 = lerp(n001, n101, f.x);
    float nx11 = lerp(n011, n111, f.x);
    float nxy0 = lerp(nx00, nx10, f.y);
    float nxy1 = lerp(nx01, nx11, f.y);
    return lerp(nxy0, nxy1, f.z);
}

float Fbm(float3 p)
{
    float lSum = 0.0f;
    float lAmp = 0.5f;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        lSum += lAmp * ValueNoise(p);
        p *= 2.02f;
        lAmp *= 0.5f;
    }
    return lSum;
}