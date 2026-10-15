// ============================================================
// FogPS.hlsl
// Volumetric fog pass (STEP 3: raymarched height fog + light shafts).
// Per march step, asks the cube-array shadow map whether the point
// is lit; lit fog in-scatters light toward the camera weighted by a
// Henyey-Greenstein phase, producing volumetric light shafts.
// Author: Chan WingChung
// ============================================================
#include "LightCommon.hlsli"

// --- b0 is CB_WVP, b1/b2 are lights/shadow (via LightCommon) -> use b3
cbuffer CB_FOG : register(b3)
{
    float4x4 invViewProj;
    float3 camPos;
    float density;
    float3 fogColor;
    float heightFalloff;
    float baseHeight;
    float maxDistance;
    int marchSteps;
    float scatterIntensity;
    float phaseG;
    float fogAmbient;
    float resolutionScale;
    float _pad0;
};

// --- t0 is g_texture, t2 is the shadow cube array -> use t1
Texture2D<float> depthTex : register(t1);

struct VS_OUTPUT_FOG
{
    float4 svPosition : SV_POSITION;
    float2 clip : TEXCOORD0;
};

// ------------------------------------------------------------
// HeightDensity : fog density at a world position
// ------------------------------------------------------------
float HeightDensity(float3 worldPos)
{
    return density * exp(-heightFalloff * max(worldPos.y - baseHeight, 0.0f));
}

// ------------------------------------------------------------
// FogShadow : 1-tap version of CalcShadowForLight
// (9-tap PCF per march step would be ~9x the cost; fog does not
//  need soft edges because the march itself blurs the result)
// ------------------------------------------------------------
float FogShadow(int lightIndex, float3 worldPos)
{
    if (lightIndex >= shadowCount)
        return 1.0f;

    float3 lToPixel = worldPos - shadows[lightIndex].lightPos;
    float lCurrentDist = length(lToPixel);
    float lFarZ = shadows[lightIndex].farZ;

    // --- outside the shadow volume: no occlusion info -> assume lit
    if (lCurrentDist < 0.01f || lCurrentDist > lFarZ * 0.98f)
        return 1.0f;

    float lBias = max(0.05f, lCurrentDist * 0.02f);
    float lClosestDist = g_shadowCubeArray.Sample(
        g_shadowSampler, float4(lToPixel, lightIndex)).r * lFarZ;

    return (lCurrentDist - lBias > lClosestDist) ? 0.0f : 1.0f;
}

// ------------------------------------------------------------
// HenyeyGreenstein : forward-scattering phase (same as CloudPS)
// ------------------------------------------------------------
float HenyeyGreenstein(float cosAngle, float g)
{
    float g2 = g * g;
    return (1.0f - g2) / (4.0f * 3.14159265f * pow(1.0f + g2 - 2.0f * g * cosAngle, 1.5f));
}

float4 main(VS_OUTPUT_FOG input) : SV_TARGET
{
    float lDepth = depthTex.Load(int3(input.svPosition.xy * resolutionScale, 0)).r;
    
    // --- rebuild the surface point (sky pixels rebuild the far plane)
    float4 lWorldH = mul(float4(input.clip, lDepth, 1.0f), invViewProj);
    float3 lWorldPos = lWorldH.xyz / lWorldH.w;

    // --- ray setup
    float3 lRayVec = lWorldPos - camPos;
    float lSurfaceDist = length(lRayVec);
    float3 lRayDir = lRayVec / lSurfaceDist;
    float lRayLen = min(lSurfaceDist, maxDistance);

    float lStepLen = lRayLen / (float) marchSteps;
    
    // --- per-pixel jitter (interleaved gradient noise): banding -> fine grain
    float lJitter = frac(52.9829189f * frac(dot(input.svPosition.xy,
                         float2(0.06711056f, 0.00583715f))));
    float3 lPos = camPos + lRayDir * (lStepLen * lJitter);
    // --- march: absorption + in-scattering (same shape as CloudPS main)
    float lTransmittance = 1.0f;
    float3 lScatter = float3(0.0f, 0.0f, 0.0f);

    [loop]
    for (int i = 0; i < marchSteps; ++i)
    {
        float lSigma = HeightDensity(lPos);
        if (lSigma > 0.0f)
        {
            // --- ambient: kept LOW so lit/shadowed contrast survives
            float3 lLit = fogColor * fogAmbient;

            // --- in-scattering from each light that reaches this point
            for (int lL = 0; lL < lightCount; ++lL)
            {
                LIGHT_RESULT lLR = CalcLightDir(lights[lL], lPos);
                if (lLR.attenuation <= 0.0f)
                    continue;

                float lShadow = FogShadow(lL, lPos);
                if (lShadow <= 0.0f)
                    continue;

                float lPhase = HenyeyGreenstein(dot(lRayDir, lLR.L), phaseG);
                lLit += lights[lL].lightColor * lights[lL].intensity
                      * lLR.attenuation * lShadow * lPhase * scatterIntensity;
            }

            lScatter += lLit * lSigma * lStepLen * lTransmittance;
            lTransmittance *= exp(-lSigma * lStepLen);

            if (lTransmittance < 0.01f)  // fog is opaque beyond here -> stop
                break;
        }
        lPos += lRayDir * lStepLen;
    }

    return float4(ToneMapAces(lScatter), saturate(1.0f - lTransmittance));
}