// Terrain.hlsl

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gHeightMap : register(t2);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per object
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
    float gTesselationFactor;
    float gHeightMapScale;
};

// Constant data that varies per frame
cbuffer cbPass : register(b1)
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

// Constant data that varies per material
cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float gMetallic;
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
    float3 PosW : POSITION;
    float2 TexC : TEXCOORD;
    float3 Normal : NORMAL;
};


inline float2 ApplyTexTransform(float2 uv)
{
    float3 t = mul(float4(uv, 1.0f, 1.0f), gTexTransform).xyz;
    return t.xy;
}


DS_VS_OUTPUT_GS_INPUT VS(VS_INPUT vin)
{
    DS_VS_OUTPUT_GS_INPUT vout = (DS_VS_OUTPUT_GS_INPUT) 0.0f;
	
    float4 posW = mul(float4(vin.Pos, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    vout.Normal = normalize(mul(vin.Normal, (float3x3) gWorld));
	
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;
    
    float fDisplacement = gHeightMap.SampleLevel(gsamAnisotropicClamp, vout.TexC, 0).r;
    vout.PosW += float3(0, 1, 0) * fDisplacement * gHeightMapScale;

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
        origPosCS[i] = mul(float4(input[i].PosW, 1.0), gViewProj);
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
            i_down.PosCS = mul(float4(i_down.PosW, 1.0), gViewProj);
            i_down.Normal = input[i].Normal;
            i_down.TexC = input[i].TexC;
            
            j_down.PosW = input[j].PosW - float3(0, curtainHeight, 0);
            j_down.PosCS = mul(float4(j_down.PosW, 1.0), gViewProj);
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

struct GBufferData
{
    float4 diffuse : SV_TARGET0;
    float4 emissive : SV_TARGET1;
    float4 normal : SV_TARGET2;
    float4 materialAlbedo : SV_TARGET3;
    float4 MaterialFresnelRoughness : SV_TARGET4;
};

float smoothBand(float x, float edge0, float edge1)
{
    float t = saturate((x - edge0) / max(1e-5, (edge1 - edge0)));
    return t * t * (3.0 - 2.0 * t);
}

GBufferData PS(GS_OUT pin)
{
    
    GBufferData pout;
    
    float2 uv = pin.TexC;
    
    float3 NormalMapSample = gNormalMap.Sample(gsamAnisotropicClamp, uv).rgb;
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
    pout.normal = float4(NormalMapSample, gMetallic);
    pout.materialAlbedo = gDiffuseAlbedo;
    pout.MaterialFresnelRoughness = float4(gFresnelR0, gRoughness);

    return pout;
}
