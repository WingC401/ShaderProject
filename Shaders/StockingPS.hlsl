// ============================================================
// StockingPS.hlsl
// Pixel shader for sheer nylon stockings over Phong skin
// (Beer-Lambert coverage + Charlie sheen + anisotropic GGX)
// Author: Chan WingChung
// ============================================================
#include "LightCommon.hlsli"

static const float STOCKING_F0 = 0.04f;              // nylon ~ IOR 1.5
static const float STOCKING_EDGE_EXPONENT = 2.0f;    // grazing-angle darkening curve
static const float STOCKING_SHEEN_ROUGHNESS = 0.35f; // Charlie lobe width

// ------------------------------------------------------------
// CalcCoverage : Beer-Lambert through a thin thread layer
// path length grows toward grazing angles -> darker silhouette
// ------------------------------------------------------------
float CalcCoverage(float NdotV)
{
    float lDensity = denier / 60.0f;   // 40D ~= 50% head-on
    float lPathLength = 1.0f / pow(max(NdotV, 0.02f), STOCKING_EDGE_EXPONENT);
    return 1.0f - exp(-lDensity * lPathLength);
}

// ------------------------------------------------------------
// DistributionCharlie / VisibilityAshikhmin : sheen lobe (Estevez & Kulla 2017)
// ------------------------------------------------------------
float DistributionCharlie(float NdotH, float roughness)
{
    float lAlpha = max(roughness, 0.02f);
    float lInvAlpha = 1.0f / lAlpha;
    float lSin2 = 1.0f - NdotH * NdotH;
    return (2.0f + lInvAlpha) * pow(lSin2, lInvAlpha * 0.5f) / (2.0f * PI);
}

float VisibilityAshikhmin(float NdotL, float NdotV)
{
    return 1.0f / (4.0f * (NdotL + NdotV - NdotL * NdotV) + 1e-4f);
}

// ------------------------------------------------------------
// DistributionGgxAnisotropic / VisibilitySmithGgxAnisotropic : Burley 2012
// ------------------------------------------------------------
float DistributionGgxAnisotropic(float at, float ab, float TdotH, float BdotH, float NdotH)
{
    float lA2 = at * ab;
    float3 lD = float3(ab * TdotH, at * BdotH, lA2 * NdotH);
    float lD2 = dot(lD, lD);
    float lW2 = lA2 / max(lD2, 1e-7f);
    return lA2 * lW2 * lW2 / PI;
}

float VisibilitySmithGgxAnisotropic(float at, float ab,
    float TdotV, float BdotV, float TdotL, float BdotL, float NdotV, float NdotL)
{
    float lLambdaV = NdotL * length(float3(at * TdotV, ab * BdotV, NdotV));
    float lLambdaL = NdotV * length(float3(at * TdotL, ab * BdotL, NdotL));
    return 0.5f / max(lLambdaV + lLambdaL, 1e-5f);
}

// ------------------------------------------------------------
// FresnelSchlick : Schlick approximation of Fresnel
// ------------------------------------------------------------
float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - cosTheta, 5.0f);
}

float4 main(PS_INPUT_NORMAL_MAP input) : SV_TARGET
{
    // --- Tangent frame : T around the leg, B along the leg
    float3 lN = normalize(input.worldNormal);
    float3 lT = normalize(input.worldTangent);
    lT = normalize(lT - lN * dot(lN, lT));
    float3 lB = cross(lN, lT);

    float3 lV = normalize(cameraPos - input.worldPos);
    float  lNdotV = saturate(dot(lN, lV));
    float  lTdotV = dot(lT, lV);
    float  lBdotV = dot(lB, lV);

    float lGlossRough = max(glossRoughness, 0.02f);
    float lAt = max(lGlossRough * (1.0f + anisotropy), 0.001f);
    float lAb = max(lGlossRough * (1.0f - anisotropy), 0.001f);

    float4 lBaseColor = useTexture
        ? g_texture.Sample(g_sampler, input.uv)
        : materialColor;

    float3 lSkin   = lBaseColor.rgb * ambientStrength;
    float3 lFabric = stockingColor  * ambientStrength;
    float3 lSheen  = float3(0.0f, 0.0f, 0.0f);
    float3 lGloss  = float3(0.0f, 0.0f, 0.0f);

    // --- Direct lighting : skin (Phong), fabric (Lambert), sheen, gloss
    [loop]
    for (int lI = 0; lI < lightCount; lI++)
    {
        float lShadow = CalcShadowForLight(lI, input.worldPos);

        lSkin += CalcPhongContribution(lights[lI], lN, lV, input.worldPos,
                    lBaseColor.rgb, lShadow, shininess);

        LIGHT_RESULT lLR = CalcLightDir(lights[lI], input.worldPos);
        float  lNdotL    = saturate(dot(lN, lLR.L));
        float3 lRadiance = lights[lI].lightColor * lights[lI].intensity
                         * lLR.attenuation * lShadow;

        lFabric += stockingColor * lNdotL * lRadiance;

        float3 lH     = normalize(lLR.L + lV);
        float  lNdotH = saturate(dot(lN, lH));
        float  lVdotH = saturate(dot(lV, lH));

        float lDSheen = DistributionCharlie(lNdotH, STOCKING_SHEEN_ROUGHNESS);
        float lVSheen = VisibilityAshikhmin(lNdotL, lNdotV);
        lSheen += sheenIntensity * lDSheen * lVSheen * lNdotL * lRadiance;

        float  lDGloss = DistributionGgxAnisotropic(lAt, lAb, dot(lT, lH), dot(lB, lH), lNdotH);
        float  lVGloss = VisibilitySmithGgxAnisotropic(lAt, lAb, lTdotV, lBdotV,
                             dot(lT, lLR.L), dot(lB, lLR.L), lNdotV, lNdotL);
        float3 lFGloss = FresnelSchlick(lVdotH, STOCKING_F0.xxx);
        lGloss += glossIntensity * lDGloss * lVGloss * lFGloss * lNdotL * lRadiance;
    }

    // --- Coverage and composite
    float lCoverage = CalcCoverage(lNdotV);

    float3 lLinearColor = lSkin;
    if (stockingEnabled != 0)
        lLinearColor = lerp(lSkin, lFabric, lCoverage) + (lSheen + lGloss) * lCoverage;

    // --- Debug views
    if (debugMode == 1) return float4(lCoverage.xxx, 1.0f);
    if (debugMode == 2) return float4(lNdotV.xxx, 1.0f);
    if (debugMode == 3) return float4(GammaCorrect(ToneMapAces(lSheen)), 1.0f);
    if (debugMode == 4) return float4(GammaCorrect(ToneMapAces(lGloss)), 1.0f);
    if (debugMode == 5) return float4(GammaCorrect(ToneMapAces(lFabric)), 1.0f);
    if (debugMode == 6) return float4(lN * 0.5f + 0.5f, 1.0f);

    lLinearColor += CalcRimLight(lN, lV);
    lLinearColor += ApplyDissolve(input.uv);
    float3 lFinal = GammaCorrect(ToneMapAces(lLinearColor));

    return float4(lFinal, lBaseColor.a);
}
