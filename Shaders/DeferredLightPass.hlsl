// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

Texture2D   gDiffuseMap     : register(t0);
Texture2D   gEmissiveMap    : register(t1);
Texture2D   gNormalMap      : register(t2);
Texture2D   gMaterialAlbedoMap : register(t3);
Texture2D   gMaterialFresnelRoughnessMap : register(t4);

Texture2D gShadowMap : register(t5);

SamplerComparisonState gShadowSampler : register(s0);

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
    float4x4 View;
    float4x4 Proj;
    float4x4 ShadowTransform;
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

float CalcShadowFactor(float4 shadowPosH)
{
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;

    // Depth in NDC space.
    float depth = shadowPosH.z;

    uint width, height, numMips;
    gShadowMap.GetDimensions(0, width, height, numMips);

    // Texel size.
    float dx = 1.0f / (float) width;

    float percentLit = 0.0f;
    const float2 offsets[9] =
    {
        float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx, +dx), float2(0.0f, +dx), float2(dx, +dx)
    };

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        percentLit += gShadowMap.SampleCmpLevelZero(gShadowSampler,
            shadowPosH.xy + offsets[i], depth).r;
    }
    
    return percentLit / 9.0f;
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
    
    float3 WorldPosition = ReconstructWorldPosition(UV, Emissive.w);
    float3 MatFresnelR0 = MatParams.xyz;
    float MatRoughness = MatParams.w;
    float3 Normal = NormalChannel.rgb;
    
    
    
    // Vector from point being lit to eye.
    float3 toEyeW = gEyePosW - WorldPosition;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize

    const float shininess = 1.0f - MatRoughness;
    Material mat = { Diffuse, MatFresnelR0, shininess };
    
    float3 shadowFactor = float3(1.0f, 1.0f, 1.0f);
    //shadowFactor[0] = CalcShadowFactor(ShadowPos);

    float3 directLight;
    
    //discard if there is no geometry in Gbuffer at current pixel
    if (length(Normal) < 0.01f)
        discard;
    
    //calculate light based on its type
    if(CurrentLight.LightType == 0)
    {
        directLight = shadowFactor * ComputeDirectionalLight(CurrentLight, mat, Normal, toEyeW) * CurrentLight.Color;
    }
    else if (CurrentLight.LightType == 1)
    {
        if (length(CurrentLight.Position - WorldPosition) > (CurrentLight.Strength.x * 7))
            discard;
        
        directLight = shadowFactor * ComputePointLight(CurrentLight, mat, WorldPosition, Normal, toEyeW) * CurrentLight.Color;
        //return float4(UV, 0.f, 1.f);
    }
    else if(CurrentLight.LightType == 2)
    {
        directLight = shadowFactor * ComputeSpotLight(CurrentLight, mat, WorldPosition, Normal, toEyeW) * CurrentLight.Color;
        //return float4(UV, 0.f, 1.f);
    }

    float4 litColor = float4(directLight, 0.f);

    litColor.a = Diffuse.a;

    return litColor;    
}

float4 PS_AddAmbient(VertexOut pin) : SV_Target
{
    //same as PS but only adds ambient light
    float4 MatAlbedo = gMaterialAlbedoMap.Load(int3(pin.PosH.xy, 0));
    float4 DiffuseAlbedo = gDiffuseMap.Load(int3(pin.PosH.xy, 0)) * MatAlbedo;

    float4 ambient = gAmbientLight * DiffuseAlbedo;

    float4 litColor = ambient;

    litColor.a = DiffuseAlbedo.a;

    return litColor;
}