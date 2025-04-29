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
    vout.TexC.y = 1.f - vout.TexC.y;
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
    //magic DirectX texcoord mutations
    float4 clipPos;
    clipPos.x = texCoord.x * 2.0f - 1.0f;
    clipPos.y = 1.0f - texCoord.y * 2.0f;
    clipPos.z = depth;
    clipPos.w = 1.0f;

    //transform into world space
    float4 viewPos = mul(clipPos, gInvViewProj);
    viewPos.xyz /= viewPos.w;

    return viewPos.xyz;
}

float4 PS(VertexOut pin) : SV_Target
{
    //loading GBufferChannels
    float4 MatAlbedo = gMaterialAlbedoMap.Sample(gsamAnisotropicWrap, pin.TexC);
    float4 MatParams = gMaterialFresnelRoughnessMap.Sample(gsamAnisotropicWrap, pin.TexC);
    float4 Emissive = gEmissiveMap.Sample(gsamAnisotropicWrap, pin.TexC);
    float4 NormalChannel = gNormalMap.Sample(gsamAnisotropicWrap, pin.TexC);
    float4 Diffuse = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC) * MatAlbedo;
    
    float3 WorldPosition = ReconstructWorldPosition(pin.TexC, Emissive.w);
    float3 MatFresnelR0 = MatParams.xyz;
    float MatRoughness = MatParams.w;
    float3 Normal = NormalChannel.rgb;
    
    // Vector from point being lit to eye.
    float3 toEyeW = gEyePosW - WorldPosition;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize

    const float shininess = 1.0f - MatRoughness;
    Material mat = { Diffuse, MatFresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float3 directLight;
    
    //calculate light based on its type
    if(CurrentLight.LightType == 0)
    {
        directLight = shadowFactor * ComputeDirectionalLight(CurrentLight, mat, Normal, toEyeW) * CurrentLight.Color;
    }
    if (CurrentLight.LightType == 1)
    {
        directLight = shadowFactor * ComputePointLight(CurrentLight, mat, WorldPosition, Normal, toEyeW) * CurrentLight.Color;
    }
    if (CurrentLight.LightType == 2)
    {
        directLight = shadowFactor * ComputeSpotLight(CurrentLight, mat, WorldPosition, Normal, toEyeW) * CurrentLight.Color;
    }

    float4 litColor = float4(directLight, 0.f);

    litColor.a = Diffuse.a;

    return litColor;    
}

float4 PS_AddAmbient(VertexOut pin) : SV_Target
{
    //same as PS but only adds ambient light
    float4 MatAlbedo = gMaterialAlbedoMap.Sample(gsamAnisotropicWrap, pin.TexC);
    float4 DiffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC) * MatAlbedo;

    float4 ambient = gAmbientLight * DiffuseAlbedo;

    float4 litColor = ambient;

    litColor.a = DiffuseAlbedo.a;

    return litColor;
}