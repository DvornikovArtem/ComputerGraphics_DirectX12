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
	
    // Transform to world space.
    float4 posW = mul(float4(vin.Pos, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
    vout.Normal = normalize(mul(vin.Normal, (float3x3) gWorld));
	
	// Output vertex attributes for interpolation across triangle.
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;

    return vout;
}

struct HS_CONSTANT_DATA_OUTPUT
{
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};


float TessFromDist(float3 a, float3 b)
{
    float3 mid = 0.5f * (a + b);
    float dist = distance(mid, gEyePosW);

    // Use the factor already provided from C++, but with a default fallback
    float maxTess = (gTesselationFactor > 0.0f) ? gTesselationFactor : 12.0f;
    maxTess = clamp(maxTess, 1.0f, 64.0f); // [maxtessfactor(64)]

    // Threshold curve: close — high, far — 1
    const float nearDist = 50.0f; // for oneself
    const float farDist = 400.0f; // for oneself
    float t = saturate((dist - nearDist) / (farDist - nearDist));
    return lerp(maxTess, 1.0f, t);
}

HS_CONSTANT_DATA_OUTPUT ConstantsHS(InputPatch<DS_VS_OUTPUT_GS_INPUT, 3> Patch, uint PatchID : SV_PrimitiveID)
{
    HS_CONSTANT_DATA_OUTPUT Out;
    
    // Backface Culling
    float3 vEdge0 = Patch[1].PosW - Patch[0].PosW;
    float3 vEdge2 = Patch[2].PosW - Patch[0].PosW;
    float3 vFaceNormal = normalize(cross(vEdge2, vEdge0));
    float3 vView = normalize(Patch[0].PosW - gEyePosW);
    
    // A negative dot product means facing away from view direction.
    // Use a small epsilon to avoid popping, since displaced vertices
    // may still be visible with dot product = 0
    if (dot(vView, vFaceNormal) < -0.25)
    {
        Out.Edges[0] = 0;
        Out.Edges[1] = 0;
        Out.Edges[2] = 0;
        Out.Inside = 0;
        return Out; // early exit
    }
    
    // Assign tessellation factors – in this case use a global
    // tessellation factor for all edges and the inside. These are
    // constant for the whole mesh.
    
    float tessfactor = 1.f;
    
    Out.Edges[0] = tessfactor;
    Out.Edges[1] = tessfactor;
    Out.Edges[2] = tessfactor;
    Out.Inside = tessfactor;
    
    /*float3 p0 = Patch[0].PosW;
    float3 p1 = Patch[1].PosW;
    float3 p2 = Patch[2].PosW;

    Out.Edges[0] = TessFromDist(p0, p1);
    Out.Edges[1] = TessFromDist(p1, p2);
    Out.Edges[2] = TessFromDist(p2, p0);
    Out.Inside = (Out.Edges[0] + Out.Edges[1] + Out.Edges[2]) / 3.0f;*/
    
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
    float3 vWorldPos =
    BarycentricCoordinates.x * TrianglePatch[0].vWorldPos +
    BarycentricCoordinates.y * TrianglePatch[1].vWorldPos +
    BarycentricCoordinates.z * TrianglePatch[2].vWorldPos;
    Out.PosW = vWorldPos;
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

    
    // sample the displacement map for the magnitude of displacement
    //float fDisplacement = gHeightMap.SampleLevel(gsamAnisotropicWrap, Out.TexC.xy, 0).r;
    float fDisplacement = gHeightMap.SampleLevel(gsamAnisotropicClamp, Out.TexC.xy, 0).r;
    
    // translate the position
    vWorldPos += float3(0, 1, 0) * fDisplacement * gHeightMapScale;
    Out.PosW = vWorldPos;
    return Out;
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

GBufferData PS(GS_OUT pin)
{
    
    GBufferData pout;
    
    float2 uv = pin.TexC;
    
    float3 NormalMapSample = gNormalMap.Sample(gsamAnisotropicClamp, uv).rgb;
    float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicClamp, uv);

    pout.diffuse = diffuseAlbedo;
    pout.emissive = float4(0.f, 0.f, 0.f, pin.PosCS.z); //xyz is free for now
    pout.normal = float4(NormalMapSample, gMetallic); //w is free for now
    pout.materialAlbedo = gDiffuseAlbedo;
    pout.MaterialFresnelRoughness = float4(gFresnelR0, gRoughness);

    return pout;
}
