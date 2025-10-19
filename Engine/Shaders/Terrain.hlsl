#include "CBufferStructures.hlsl"

Texture2D DiffuseMap : register(t0);
Texture2D NormalMap : register(t1);
Texture2D HeightMap : register(t2);

SamplerState samPointWrap : register(s0);
SamplerState samPointClamp : register(s1);
SamplerState samLinearWrap : register(s2);
SamplerState samLinearClamp : register(s3);
SamplerState samAnisotropicWrap : register(s4);
SamplerState samAnisotropicClamp : register(s5);

ConstantBuffer<ObjectCB> cbObject : register(b0);

ConstantBuffer<MainPassCB> cbMainPass : register(b1);

ConstantBuffer<MaterialCB> cbMaterial : register(b2);

struct VS_INPUT
{
    float3 Pos : POSITION;
    float2 TexC : TEXCOORD;
    float3 Normal : NORMAL;
};

struct DS_VS_OUTPUT_GS_INPUT
{
    float3 PosW : POSITION;
    float2 TexC : TEXCOORD;
    float3 Normal : NORMAL;
};


DS_VS_OUTPUT_GS_INPUT VS(VS_INPUT vin)
{
    DS_VS_OUTPUT_GS_INPUT vout = (DS_VS_OUTPUT_GS_INPUT) 0.0f;
	
    float4 posW = mul(float4(vin.Pos, 1.0f), cbObject.World);
    vout.PosW = posW.xyz;

    vout.Normal = normalize(mul(vin.Normal, (float3x3) cbObject.World));
	
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), cbObject.TexTransform);
    vout.TexC = mul(texC, cbMaterial.MatTransform).xy;
    
    float fDisplacement = HeightMap.SampleLevel(samAnisotropicClamp, vout.TexC, 0).r;
    vout.PosW += float3(0, 1, 0) * fDisplacement * cbObject.HeightMapScale;

    return vout;
}

struct GS_OUT
{
    float4 PosCS : SV_POSITION;
    float3 PosW : POSITION;
    float2 TexC : TEXCOORD;
    float3 Normal : NORMAL;
};

// Check if edge is on border
bool isBorderEdge(float2 t0, float2 t1)
{
    const float epsilon = 0.001;
    if (abs(t0.x) < epsilon && abs(t1.x) < epsilon)
        return true;
    if (abs(t0.x - 1.0) < epsilon && abs(t1.x - 1.0) < epsilon)
        return true;
    if (abs(t0.y) < epsilon && abs(t1.y) < epsilon)
        return true;
    if (abs(t0.y - 1.0) < epsilon && abs(t1.y - 1.0) < epsilon)
        return true;
    return false;
}

[maxvertexcount(21)]
void GS(triangle DS_VS_OUTPUT_GS_INPUT input[3], inout TriangleStream<GS_OUT> stream)
{
    GS_OUT output;
    
    // Precompute clip space positions for original vertices
    float4 origPosCS[3];
    for (int i = 0; i < 3; i++)
    {
        origPosCS[i] = mul(float4(input[i].PosW, 1.0), cbMainPass.ViewProj);
    }
    
    // Output original triangle
    for (int i = 0; i < 3; i++)
    {
        output.PosCS = origPosCS[i];
        output.PosW = input[i].PosW;
        output.Normal = input[i].Normal;
        output.TexC = input[i].TexC;
        stream.Append(output);
    }
    
    float curtainHeight = 50.0; // ADJUST ME
    int edges[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
    
    for (int e = 0; e < 3; e++)
    {
        int i = edges[e][0];
        int j = edges[e][1];
        
        if (isBorderEdge(input[i].TexC, input[j].TexC))
        {
            // Create down vertices
            GS_OUT i_down, j_down;
            
            i_down.PosW = input[i].PosW - float3(0, curtainHeight, 0);
            i_down.PosCS = mul(float4(i_down.PosW, 1.0), cbMainPass.ViewProj);
            i_down.Normal = input[i].Normal;
            i_down.TexC = input[i].TexC;
            
            j_down.PosW = input[j].PosW - float3(0, curtainHeight, 0);
            j_down.PosCS = mul(float4(j_down.PosW, 1.0), cbMainPass.ViewProj);
            j_down.Normal = input[j].Normal;
            j_down.TexC = input[j].TexC;
            
            // First curtain triangle
            output.PosCS = origPosCS[i];
            output.PosW = input[i].PosW;
            output.Normal = input[i].Normal;
            output.TexC = input[i].TexC;
            stream.Append(output);
            
            output.PosCS = origPosCS[j];
            output.PosW = input[j].PosW;
            output.Normal = input[j].Normal;
            output.TexC = input[j].TexC;
            stream.Append(output);
            
            output.PosCS = j_down.PosCS;
            output.PosW = j_down.PosW;
            output.Normal = j_down.Normal;
            output.TexC = j_down.TexC;
            stream.Append(output);
            
            // Second curtain triangle
            output.PosCS = origPosCS[i];
            output.PosW = input[i].PosW;
            output.Normal = input[i].Normal;
            output.TexC = input[i].TexC;
            stream.Append(output);
            
            output.PosCS = i_down.PosCS;
            output.PosW = i_down.PosW;
            output.Normal = i_down.Normal;
            output.TexC = i_down.TexC;
            stream.Append(output);
            
            output.PosCS = j_down.PosCS;
            output.PosW = j_down.PosW;
            output.Normal = j_down.Normal;
            output.TexC = j_down.TexC;
            stream.Append(output);
        }
    }
}

float smoothBand(float x, float edge0, float edge1)
{
    float t = saturate((x - edge0) / max(1e-5, (edge1 - edge0)));
    return t * t * (3.0 - 2.0 * t);
}

GBufferData PS(GS_OUT pin)
{
    
    GBufferData pout;
    
    float2 uv = pin.TexC;
    
    float3 NormalMapSample = NormalMap.Sample(samAnisotropicClamp, uv).rgb;
    float4 diffuseAlbedo;
    
    const float SEA_LEVEL = 900.0;
    const float GRASS_HEIGHT = 1200.0;
    const float SNOW_HEIGHT = 1300.0;
    const float HEIGHT_BLEND_WIDTH = 5.0;

    const float ROCK_SLOPE_START = 0.7;
    const float ROCK_SLOPE_END = 0.3;

    const float SNOW_SLOPE_REDUCTION = 0.6;

    const float3 SEA_COLOR = float3(0.02, 0.12, 0.25);
    const float3 LAND_COLOR = float3(0.18, 0.35, 0.12);
    const float3 ROCK_COLOR = float3(0.35, 0.33, 0.32);
    const float3 SNW_COLOR = float3(0.92, 0.92, 0.97);

    float heightY = pin.PosW.y;
    float normalY = saturate(NormalMapSample.y);
    float4 w;
    
    float seaW = 1.0 - smoothBand(heightY, SEA_LEVEL, SEA_LEVEL + HEIGHT_BLEND_WIDTH);
    seaW = saturate(seaW);

    float landUp = smoothBand(heightY, SEA_LEVEL, HEIGHT_BLEND_WIDTH);
    float landDown = 1.0 - smoothBand(heightY, GRASS_HEIGHT, GRASS_HEIGHT + HEIGHT_BLEND_WIDTH);
    float landW = saturate(landUp * landDown);

    float snowW = smoothBand(heightY, SNOW_HEIGHT - HEIGHT_BLEND_WIDTH, SNOW_HEIGHT + HEIGHT_BLEND_WIDTH);

    float slopeT = saturate((ROCK_SLOPE_START - normalY) / max(1e-5, (ROCK_SLOPE_START - ROCK_SLOPE_END)));
    float rockW = smoothstep(0.0, 1.0, slopeT);

    snowW *= lerp(1.0, normalY, saturate(SNOW_SLOPE_REDUCTION));

    seaW *= smoothstep(ROCK_SLOPE_END, ROCK_SLOPE_START, normalY);

    float sum = seaW + landW + rockW + snowW + 1e-6;
    w = float4(seaW, landW, rockW, snowW) / sum;

    float3 blended = SEA_COLOR * w.x + LAND_COLOR * w.y + ROCK_COLOR * w.z + SNW_COLOR * w.w;
    

    pout.diffuse = float4(blended, 1.0);
    pout.emissive = float4(0.f, 0.f, 0.f, pin.PosCS.z);
    pout.normal = float4(NormalMapSample, cbMaterial.Metallic);
    pout.MaterialFresnelRoughness = float4(cbMaterial.FresnelR0, cbMaterial.Roughness);

    return pout;
}
