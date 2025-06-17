// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

Texture2D   gDiffuseMap     : register(t0);
Texture2D   gEmissiveMap    : register(t1);
Texture2D   gNormalMap      : register(t2);
Texture2D   gMaterialAlbedoMap : register(t3);
Texture2D   gMaterialFresnelRoughnessMap : register(t4);

Texture2DArray gShadowMaps : register(t5);

TextureCube IrradianceMap   : register(t6);
TextureCube PrefilterEnvMap : register(t7);
Texture2D BRDF_LUT          : register(t8);

SamplerComparisonState gShadowSampler : register(s0);
SamplerState gsamLinearClamp          : register(s1);

// Constant data that varies per frame.
cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

	float4 gFogColor;
	float gFogStart;
	float gFogRange;
	float2 cbPerObjectPad2;
    
    float4 Decals[3];
};

// Constant data that varies per light.
cbuffer cbPerLight : register(b1)
{
    Light CurrentLight;
    float4x4 gWorld;
    float4x4 View[6];
    float4x4 Proj[6];
    float4x4 ShadowTransform[6];
    float4 CascadeDistances;
}


struct VertexIn
{
	float3 PosL    : POSITION;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
};


VertexOut VS_FSQuad(uint vertexID : SV_VertexID)
{
    //full-screen quad
    
    float2 verts[3] =
    {
        float2(-1, -1),
        float2(-1, 3),
        float2(3, -1)
    };
    
    
    VertexOut vout;
    vout.PosH = float4(verts[vertexID], 0, 1);
    return vout;
}

VertexOut VS_Bounded(VertexIn vin)
{
    VertexOut vout;
    
    vout.PosH = mul(float4(vin.PosL, 1.0f), mul(gWorld, gViewProj));
    
    return vout;
}


float3 ReconstructWorldPosition(float2 UV, float depth)
{
    //magic DirectX texcoord mutations
    float4 clipPos;
    clipPos.x = UV.x * 2.0f - 1.0f;
    clipPos.y = 1.0f - UV.y * 2.0f;
    clipPos.z = depth;
    clipPos.w = 1.0f;

    //transform into world space
    float4 viewPos = mul(clipPos, gInvViewProj);
    viewPos.xyz /= viewPos.w;

    return viewPos.xyz;
}

float CalcShadowFactor(float3 WorldPosition, float3 Normal, uint ShadowMapIndex)
{
    float4 shadowPosH = mul(float4(WorldPosition, 1.f), ShadowTransform[ShadowMapIndex]);
    
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;
    
    // Depth in NDC space.
    float depth = shadowPosH.z;

    uint width, height, numLayers, numMips;
    gShadowMaps.GetDimensions(0, width, height, numLayers, numMips);

    // Slope-Scaled Depth Bias
    float3 lightDir = normalize(-CurrentLight.Direction);
    float slopeBias = 0.005 * tan(acos(saturate(dot(Normal, lightDir))));
    slopeBias = clamp(slopeBias, 0.001, 0.05);
    float biasedDepth = depth - (0.001 + slopeBias);
    
    //PCF
    float dx = 1.f / (float) width;

    float percentLit = 0.0f;
    float totalWeight = 0.0f;
    const float2 offsets[9] =
    {
        float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx, +dx), float2(0.0f, +dx), float2(dx, +dx)
    };

    // Weights for smoother filtering
    const float weights[9] =
    {
        0.0625, 0.125, 0.0625,
        0.125, 0.25, 0.125,
        0.0625, 0.125, 0.0625
    };

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float2 sampleCoord = shadowPosH.xy + offsets[i];
        
        // Skip samples outside the shadow map
        if (sampleCoord.x >= 0.f && sampleCoord.x <= 1.f &&
            sampleCoord.y >= 0.f && sampleCoord.y <= 1.f)
        {
            percentLit += gShadowMaps.SampleCmpLevelZero(gShadowSampler,
                float3(sampleCoord, ShadowMapIndex), biasedDepth).r * weights[i];
            totalWeight += weights[i];
        }
    }
    
    // Normalize by actual weight sum (in case some samples were skipped)
    return totalWeight > 0 ? percentLit / totalWeight : 1.0f;
}

#define PI 3.14159265359

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float4 PS(VertexOut pin) : SV_Target
{
    float2 UV = pin.PosH.xy / gRenderTargetSize;
    uint2 TexelCoord = pin.PosH.xy;
    //loading GBuffer channels
    float4 MatAlbedo = gMaterialAlbedoMap.Load(int3(TexelCoord, 0));
    float4 MatParams = gMaterialFresnelRoughnessMap.Load(int3(TexelCoord, 0));
    float4 Emissive = gEmissiveMap.Load(int3(TexelCoord, 0));
    float4 NormalChannel = gNormalMap.Load(int3(TexelCoord, 0));
    float4 Diffuse = gDiffuseMap.Load(int3(TexelCoord, 0)) * MatAlbedo;
    float Metallic = NormalChannel.w;

    float3 WorldPosition = ReconstructWorldPosition(UV, Emissive.w);
    float3 MatFresnelR0 = MatParams.xyz;
    float MatRoughness = MatParams.w;
    float3 Normal = NormalChannel.rgb;
    
    // Vector from point being lit to eye.
    float3 toEyeW = gEyePosW - WorldPosition;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize
    float NdotV = max(dot(Normal, toEyeW), 0.0);

    const float shininess = 1.0f - MatRoughness;
    Material mat = { Diffuse, MatFresnelR0, shininess };
    
    //discard if there is no geometry in Gbuffer at current pixel
    if (length(Normal) < 0.01f)
        discard;
    
    float3 Lighting;
    
    //calculate light based on its type
    if (CurrentLight.LightType == 0)
    {
        // Расчет теней (оставляем существующую логику)
        float shadowFactor = 1.f;
        float distanceFromEye = length(WorldPosition - gEyePosW);
        
        for (uint cascade = 0; cascade < 4; cascade++)
        {
            float factor = CalcShadowFactor(WorldPosition, Normal, cascade);
            if (factor < 0.3f)
            {
                shadowFactor = factor;
                break;
            }
        }
        
        // PBR расчет для Directional Light
        float3 lightDir = normalize(-CurrentLight.Direction);
        float3 halfVec = normalize(toEyeW + lightDir);
        float NdotL = max(dot(Normal, lightDir), 0.0);
        
        // Френель
        float3 F0 = lerp(0.04.xxx, Diffuse.rgb, Metallic);
        float3 F = FresnelSchlick(max(dot(halfVec, toEyeW), 0.0), F0);
        
        // Распределение нормалей (NDF)
        float NDF = DistributionGGX(Normal, halfVec, MatRoughness);
        
        // Геометрия
        float G = GeometrySmith(Normal, toEyeW, lightDir, MatRoughness);
        
        // Cook-Torrance BRDF
        float3 numerator = NDF * G * F;
        float denominator = 4.0 * NdotV * NdotL + 0.001;
        float3 specular = numerator / denominator;
        
        // Коэффициенты
        float3 kS = F;
        float3 kD = 1.0 - kS;
        kD *= (1.0 - Metallic);
        
        // Радианс света
        float3 radiance = CurrentLight.Strength * CurrentLight.Color;
        
        // Финальный вклад света
        float3 Lo = (kD * Diffuse.rgb / PI + specular) * radiance * NdotL;
        
        Lighting = shadowFactor * Lo;
    }
    else if (CurrentLight.LightType == 1)
    {
        if (length(CurrentLight.Position - WorldPosition) > (CurrentLight.Strength.x * 10))
            discard;
        
        float3 lightToPixel = WorldPosition - CurrentLight.Position;
        float distToLight = length(lightToPixel);
        lightToPixel /= distToLight;
    
        // determine shadow map cube face index
        float3 absDir = abs(lightToPixel);
        uint faceIndex = 0;
        if (absDir.x >= absDir.y && absDir.x >= absDir.z)
            faceIndex = (lightToPixel.x > 0) ? 0 : 1;
        else if (absDir.y >= absDir.z)
            faceIndex = (lightToPixel.y > 0) ? 2 : 3;
        else
            faceIndex = (lightToPixel.z > 0) ? 4 : 5;
        
        Lighting = CalcShadowFactor(WorldPosition, Normal, faceIndex) * ComputePointLight(CurrentLight, mat, WorldPosition, Normal, toEyeW) * CurrentLight.Color;
    }
    else if(CurrentLight.LightType == 2)
    {
        // no cascades or complex maps here. using shadow map 0
        Lighting = CalcShadowFactor(WorldPosition, Normal, 0) * ComputeSpotLight(CurrentLight, mat, WorldPosition, Normal, toEyeW) * CurrentLight.Color;
    }

    float4 litColor = float4(Lighting, 0.f);

    litColor.a = Diffuse.a;

    return litColor;    
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float
roughness)
{
    return F0 + (max(1.0 - roughness, F0) - F0) * pow(saturate(1.0 -
cosTheta), 5.0);
}

float4 PS_AddAmbient(VertexOut pin) : SV_Target
{
    uint2 TexelCoord = pin.PosH.xy;
    
    float4 MatAlbedo = gMaterialAlbedoMap.Load(int3(TexelCoord, 0));
    float4 MatParams = gMaterialFresnelRoughnessMap.Load(int3(TexelCoord, 0));
    float4 Emissive = gEmissiveMap.Load(int3(TexelCoord, 0));
    float4 NormalChannel = gNormalMap.Load(int3(TexelCoord, 0));
    float4 Diffuse = gDiffuseMap.Load(int3(TexelCoord, 0)) * MatAlbedo;
    
    float3 albedo = Diffuse.rgb;
    float roughness = MatParams.w;
    float metallic = NormalChannel.w;
    float3 normal = normalize(NormalChannel.rgb);
    
    if(length(normal) < 0.01f)
        discard;
    
    float2 UV = pin.PosH.xy / gRenderTargetSize;
    float3 worldPos = ReconstructWorldPosition(UV, Emissive.w);
    float3 viewDir = normalize(gEyePosW - worldPos);
    float NdotV = max(dot(normal, viewDir), 0.0);
    
    // --- PBR IBL ---
    float3 F0 = lerp(0.04.xxx, albedo, metallic);
    float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    
    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= (1.0 - metallic);
    
    float3 irradiance = IrradianceMap.Sample(gsamLinearClamp, normal).rgb;
    float3 diffuse = irradiance * albedo;
    
    const float MAX_REFLECTION_LOD = 7.0;
    float3 R = reflect(-viewDir, normal);
    float3 prefilteredColor = PrefilterEnvMap.SampleLevel(gsamLinearClamp, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = BRDF_LUT.Sample(gsamLinearClamp, float2(NdotV, roughness)).rg;
    float3 specular = prefilteredColor * (F * brdf.x + brdf.y);
    
    // Финальное ambient освещение
    float ao = 1.0f; // Можно добавить Ambient Occlusion если есть в G-буфере
    float3 ambient = (kD * diffuse + specular) * ao * 0.1f.xxx;
    
    return float4(ambient, Diffuse.a);
}