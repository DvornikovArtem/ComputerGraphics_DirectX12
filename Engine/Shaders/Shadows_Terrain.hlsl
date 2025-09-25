#include "LightingUtil.hlsl"

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gHeightMap : register(t2);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
    float gTesselationFactor;
};

// Constant data that varies per Light.
cbuffer cbPerLight : register(b1)
{
    Light CurrentLight;
    float4x4 World;
    float4x4 View[6];
    float4x4 Proj[6];
    float4x4 ShadowTransform[6];
    float4 CascadeDistances;
}

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float Pad1;
    float Pad2;
    float Pad3;
    float4x4 gMatTransform;
};

struct VS_INPUT
{
    float3 Pos : POSITION;
    float2 TexC : TEXCOORD;
    float3 Normal : NORMAL;
};

struct DS_VS_OUTPUT_GS_INPUT
{
    float3 PosW    : POSITION;
    float2 TexC    : TEXCOORD;
    float3 Normal  : NORMAL;
};

DS_VS_OUTPUT_GS_INPUT VS(VS_INPUT vin)
{
    DS_VS_OUTPUT_GS_INPUT vout = (DS_VS_OUTPUT_GS_INPUT) 0.0f;
	
    float4 posW = mul(float4(vin.Pos, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    
    vout.Normal = normalize(mul(vin.Normal, (float3x3) gWorld));
	
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;

    
    float fDisplacement = gHeightMap.SampleLevel(gsamAnisotropicClamp, vout.TexC, 0).r;
    vout.PosW += float3(0, 1, 0) * fDisplacement * 3500;
    
    return vout;
}

struct GS_OUT
{
    float4 PosCS : SV_Position;
    uint ArrInd : SV_RenderTargetArrayIndex;
    float2 TexC : TEXCOORD;
};

[instance(6)]
[maxvertexcount(3)]
void GS(triangle DS_VS_OUTPUT_GS_INPUT p[3], in uint id : SV_GSInstanceID, inout TriangleStream<GS_OUT> stream)
{
    if(CurrentLight.LightType == 0)
    {                              
        // draw 5 cascades for directional lights
                                   
        //first 5 instances only   
        if (id > 4)                
            return;
        
        for (int i = 0; i < 3; i++)
        {
            GS_OUT Out;
            Out.PosCS = mul(float4(p[i].PosW.xyz, 1.f), mul(View[id], Proj[id]));
            Out.TexC = p[i].TexC;
            Out.ArrInd = id;
            stream.Append(Out);
        }
    }
    else if (CurrentLight.LightType == 1)
    {
        // draw 6 sides of shadow map cube for point lights
        for (int i = 0; i < 3; i++)
        {
            GS_OUT Out;
            Out.PosCS = mul(float4(p[i].PosW.xyz, 1.f), mul(View[id], Proj[id]));
            Out.TexC = p[i].TexC;
            Out.ArrInd = id;
            stream.Append(Out);
        }
    }
    else if (CurrentLight.LightType == 2)
    {
        //no cascades for spot lights
        
        //first instance only
        if (id != 0)
            return;
        
        for (int i = 0; i < 3; i++)
        {
            GS_OUT Out;
            Out.PosCS = mul(float4(p[i].PosW.xyz, 1.f), mul(View[0], Proj[0]));
            Out.TexC = p[i].TexC;
            Out.ArrInd = 0;
            stream.Append(Out);
        }
    }
}

