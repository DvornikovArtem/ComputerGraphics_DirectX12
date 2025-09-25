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

    return vout;
}

struct HS_CONSTANT_DATA_OUTPUT
{
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};

HS_CONSTANT_DATA_OUTPUT ConstantsHS(InputPatch<DS_VS_OUTPUT_GS_INPUT, 3> Patch, uint PatchID : SV_PrimitiveID)
{
    HS_CONSTANT_DATA_OUTPUT Out;
    
    Out.Edges[0] = 1;
    Out.Edges[1] = 1;
    Out.Edges[2] = 1;
    Out.Inside = 1;
    
    return Out;
}

struct HS_CONTROL_POINT_OUTPUT
{
    float3 vWorldPos : POSITION;
    float2 vTexCoord : TEXCOORD;
    float3 vNormal : NORMAL;
};

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantsHS")]
[maxtessfactor(64.0)]
HS_CONTROL_POINT_OUTPUT HS(InputPatch<DS_VS_OUTPUT_GS_INPUT, 3> inputPatch, uint uCPID : SV_OutputControlPointID)
{
    HS_CONTROL_POINT_OUTPUT Out;
    Out.vWorldPos = inputPatch[uCPID].PosW.xyz;
    Out.vTexCoord = inputPatch[uCPID].TexC;
    Out.vNormal = inputPatch[uCPID].Normal;
    return Out;
}

// Called once per tessellated vertex
[domain("tri")] // indicates that triangle patches were used
// The original patch is passed in, along with the vertex position in barycentric coordinates, and the patch constant phase hull shader output(tessellation factors)
DS_VS_OUTPUT_GS_INPUT DS(HS_CONSTANT_DATA_OUTPUT input, float3 BarycentricCoordinates : SV_DomainLocation, const OutputPatch<HS_CONTROL_POINT_OUTPUT, 3> TrianglePatch)
{
    DS_VS_OUTPUT_GS_INPUT Out;
    // Interpolate world space position with barycentric coordinates
    Out.PosW =
    BarycentricCoordinates.x * TrianglePatch[0].vWorldPos +
    BarycentricCoordinates.y * TrianglePatch[1].vWorldPos +
    BarycentricCoordinates.z * TrianglePatch[2].vWorldPos;
    // Interpolate texture coordinates with barycentric coordinates
    Out.TexC =
    BarycentricCoordinates.x * TrianglePatch[0].vTexCoord +
    BarycentricCoordinates.y * TrianglePatch[1].vTexCoord +
    BarycentricCoordinates.z * TrianglePatch[2].vTexCoord;
    // Interpolate normal with barycentric coordinates
    Out.Normal =
    BarycentricCoordinates.x * TrianglePatch[0].vNormal +
    BarycentricCoordinates.y * TrianglePatch[1].vNormal +
    BarycentricCoordinates.z * TrianglePatch[2].vNormal;

    
    float fDisplacement = gHeightMap.SampleLevel(gsamAnisotropicClamp, Out.TexC.xy, 0).r;
    
    Out.PosW += float3(0, 1, 0) * fDisplacement * 3500;
    return Out;
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

