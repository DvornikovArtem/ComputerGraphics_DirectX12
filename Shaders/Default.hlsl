// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsl"

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gHeightMap : register(t2);


SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per object.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
	float4x4 gTexTransform;
    float gTesselationFactor;
};

// Constant data that varies per frame.
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

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[MaxLights];
};

// Constant data that varies per material.
cbuffer cbMaterial : register(b2)
{
	float4   gDiffuseAlbedo;
    float3   gFresnelR0;
    float    gRoughness;
	float4x4 gMatTransform;
};

// For random values
float random(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

struct VS_INPUT
{
	float3 Pos      : POSITION;
	float2 TexC     : TEXCOORD;
    float3 Normal   : NORMAL;
    float3 Tangent  : TANGENT;
};

struct DS_VS_OUTPUT_PS_INPUT
{
    float4 PosCS   : SV_POSITION;
    float3 PosW    : POSITION;
    float2 TexC    : TEXCOORD;
    float3 Normal  : NORMAL;
    float3 Tangent : TANGENT;
};

DS_VS_OUTPUT_PS_INPUT VS(VS_INPUT vin)
{
    DS_VS_OUTPUT_PS_INPUT vout = (DS_VS_OUTPUT_PS_INPUT) 0.0f;
	
    // Transform to world space.
    float4 posW = mul(float4(vin.Pos, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
    vout.Normal = normalize(mul(vin.Normal, (float3x3) gWorld));
    
    vout.Tangent = normalize(mul(vin.Tangent, (float3x3) gWorld));

    // Transform to homogeneous clip space.
    vout.PosCS = mul(posW, gViewProj);
	
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
//Called once per patch. The patch and an index to the patch (patch ID) are passed in
HS_CONSTANT_DATA_OUTPUT ConstantsHS(InputPatch<DS_VS_OUTPUT_PS_INPUT, 3> p, uint PatchID : SV_PrimitiveID)
{
    HS_CONSTANT_DATA_OUTPUT Out;
// Assign tessellation factors – in this case use a global
// tessellation factor for all edges and the inside. These are
// constant for the whole mesh.
    float TessFactor = 20.f;
    Out.Edges[0] = gTesselationFactor;
    Out.Edges[1] = gTesselationFactor;
    Out.Edges[2] = gTesselationFactor;
    Out.Inside = gTesselationFactor;
    return Out;
}

struct HS_CONTROL_POINT_OUTPUT
{
    float3 vWorldPos : POSITION;
    float2 vTexCoord : TEXCOORD;
    float3 vNormal   : NORMAL;
    float3 vTangent  : TANGENT;
};

[domain("tri")] // indicates a triangle patch (3 verts)
[partitioning("fractional_odd")] // available options: fractional_even, fractional_odd, integer, pow2
[outputtopology("triangle_cw")] // vertex ordering for the output triangles
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantsHS")] // name of the patch constant hull shader
[maxtessfactor(64.0)] //hint to the driver – the lower the better
// Pass in the input patch and an index for the control point
HS_CONTROL_POINT_OUTPUT HSMain(InputPatch<DS_VS_OUTPUT_PS_INPUT, 3> inputPatch, uint uCPID : SV_OutputControlPointID)
{
    HS_CONTROL_POINT_OUTPUT Out;
// Copy inputs to outputs – “pass through” shaders are optimal
    Out.vWorldPos = inputPatch[uCPID].PosW.xyz;
    Out.vTexCoord = inputPatch[uCPID].TexC;
    Out.vNormal = inputPatch[uCPID].Normal;
    Out.vTangent = inputPatch[uCPID].Tangent;
    return Out;
}

// Called once per tessellated vertex
[domain("tri")] // indicates that triangle patches were used
// The original patch is passed in, along with the vertex position in barycentric coordinates, and the patch constant phase hull shader output(tessellation factors)
DS_VS_OUTPUT_PS_INPUT DSMain(HS_CONSTANT_DATA_OUTPUT input, float3 BarycentricCoordinates : SV_DomainLocation, const OutputPatch<HS_CONTROL_POINT_OUTPUT, 3> TrianglePatch)
{
    DS_VS_OUTPUT_PS_INPUT Out;
    // Interpolate world space position with barycentric coordinates
    float3 vWorldPos =
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
    
    Out.Tangent =
    BarycentricCoordinates.x * TrianglePatch[0].vTangent +
    BarycentricCoordinates.y * TrianglePatch[1].vTangent +
    BarycentricCoordinates.z * TrianglePatch[2].vTangent;

    // sample the displacement map for the magnitude of displacement
    float fDisplacement = gHeightMap.SampleLevel(gsamAnisotropicWrap, Out.TexC.xy, 0).r;
    fDisplacement *= 0.3f;
    //fDisplacement += g_Bias;
    
    float3 vDirection = normalize(Out.Normal); // direction is opposite normal
    // translate the position
    Out.PosW = vWorldPos;
    vWorldPos += vDirection * fDisplacement;
    // transform to clip space
    Out.PosCS = mul(float4(vWorldPos.xyz, 1), gViewProj);
    return Out;
} 


float4 PS(DS_VS_OUTPUT_PS_INPUT pin) : SV_Target
{
    
    float2 uv = pin.TexC;
    
#ifdef ROTATINGTILES 
    float2 tileid = floor(pin.TexC);
    float2 localuv = frac(pin.TexC) - 0.5;
    float angle = tileid % 2 ? gTotalTime : -gTotalTime;
    float2 rotateduv;
    rotateduv.x = localuv.x * cos(angle) - localuv.y * sin(angle);
    rotateduv.y = localuv.x * sin(angle) + localuv.y * cos(angle);
    rotateduv += 0.5;
    uv = rotateduv;
#endif
    
    float4 diffusealbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, uv) * gDiffuseAlbedo;
    
#ifdef ALPHA_TEST
	// discard pixel if texture alpha < 0.1.  we do this test as soon 
	// as possible in the shader so that we can potentially exit the
	// shader early, thereby skipping the rest of the shader code.
	clip(diffusealbedo.a - 0.1f);
#endif
    
    float3 NormalMapSample = gNormalMap.Sample(gsamAnisotropicWrap, uv).rgb;
    float3 UnpackedNormal = NormalMapSample * 2.f - 1.f;
    // TBN
    float3 N = normalize(pin.Normal);
    float3 T = normalize(pin.Tangent);
    T = normalize(T - dot(T, N) * N);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    
    // TangentSpace to WorldSpace
    float3 BumpedNormal = normalize(mul(UnpackedNormal, TBN));

    // Use your average normal if no NormalMap is specified
    if (length(NormalMapSample) == 0.f)
        BumpedNormal = normalize(pin.Normal);
        
    // vector from point being lit to eye. 
    float3 ToEyeW = gEyePosW - pin.PosW;
    float DistToEye = length(ToEyeW);
    ToEyeW /= DistToEye; // normalize

    // light terms.
    float4 ambient = gAmbientLight * diffusealbedo;

    const float Shininess = 1.0f - gRoughness;
    Material mat = { diffusealbedo, gFresnelR0, Shininess };
    float3 shadowFactor = float3(1.f, 1.f, 1.f);
    float4 directlight = ComputeLighting(gLights, mat, pin.PosW, BumpedNormal, ToEyeW, shadowFactor);

    float4 litcolor = ambient + directlight;
   
#ifdef FOG
    float FogAmount = saturate((DistToEye - gFogStart) / gFogRange);
    litcolor = lerp(litcolor, gFogColor, FogAmount);
#endif
    
    // common convention to take alpha from diffuse albedo.
    litcolor.a = diffusealbedo.a;
    
    return litcolor;
}


