//***************************************************************************************
// Default.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Default shader, currently supports lighting.
//***************************************************************************************

// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 2
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 1
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

Texture2D   gDiffuseMap     : register(t0);
Texture2D   gEmissiveMap    : register(t1);
Texture2D   gNormalMap      : register(t2);
Texture2D   gMaterialAlbedoMap : register(t3);
Texture2D   gMaterialFresnelRoughnessMap : register(t4);


SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

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

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[MaxLights];
    
    float4 Decals[3];
};

// Constant data that varies per light.
cbuffer cbPerLight : register(b1)
{
    Light CurrentLight;
}


struct VertexIn
{
	float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION;
    float3 NormalW : NORMAL;
	float2 TexC    : TEXCOORD;
};


VertexOut VS(uint vertexID : SV_VertexID)
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
    vout.TexC = verts[vertexID] * 0.5f + 0.5f;
    vout.TexC.y = 1 - vout.TexC.y;
    return vout;
}




// Корректная линеаризация глубины с использованием параметров камеры
float LinearizeDepth(float depth, float zNear, float zFar)
{
    // Используем параметры из константного буфера
    return zNear * zFar / (zFar - depth * (zFar - zNear));
}


// Реконструкция мирового положения из глубины
float3 ReconstructWorldPosition(float2 texCoord, float depth)
{
    float linearDepth = LinearizeDepth(depth, gNearZ, gFarZ);
    //depth = linearDepth;
    float4 clipSpacePosition = float4(texCoord * 2.0f - 1.0f, depth, 1.0f);
    float4 viewSpacePosition = mul(clipSpacePosition, gInvProj);
    viewSpacePosition.xyz /= viewSpacePosition.w;
    float4 worldSpacePosition = mul(viewSpacePosition, gInvView);
    return worldSpacePosition.xyz;
}


// Если глубину храните в [0..1] (NDC depth), то НЕ линеаризуем её.
// Просто переводим: [0..1] -> [-1..1].
float3 ReconstructWorldPosition2(float2 texCoord, float depthInNDC)
{
    // Преобразуем глубину из [0..1] в NDC:
    float ndcDepth = depthInNDC * 2.0f - 1.0f;

    // Формируем координаты в clip space:
    float4 clipPos = float4(texCoord * 2.0f - 1.0f, ndcDepth, 1.0f);
    
    // При необходимости:
    //clipPos.y = -clipPos.y; // зависит от того, как настроен viewport
    
    // Переводим из clip space в view space:
    float4 viewPos = mul(clipPos, gInvProj);
    viewPos /= viewPos.w;

    // Переводим из view space в world space:
    float4 worldPos = mul(viewPos, gInvView);
    return worldPos.xyz;
}



float4 PS(VertexOut pin) : SV_Target
{
    
    
    float4 gDiffuseAlbedo = gMaterialAlbedoMap.Sample(gsamAnisotropicWrap, pin.TexC);
    float3 gFresnelR0 = gMaterialFresnelRoughnessMap.Sample(gsamAnisotropicWrap, pin.TexC).xyz;
    float gRoughness = gMaterialFresnelRoughnessMap.Sample(gsamAnisotropicWrap, pin.TexC).w;
    
    float3 normal = gNormalMap.Sample(gsamAnisotropicWrap, pin.TexC).rgb;
    float3 posw = gEmissiveMap.Sample(gsamAnisotropicWrap, pin.TexC).xyz;
    
    float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC) * gDiffuseAlbedo;

    // Vector from point being lit to eye.
    float3 toEyeW = gEyePosW - posw;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize

    // Light terms.
    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, posw,
        normal, toEyeW, shadowFactor);

    float4 litColor = directLight + ambient;

    // Common convention to take alpha from diffuse albedo.
    litColor.a = diffuseAlbedo.a;

    return litColor;    
}