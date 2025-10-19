#include "CBufferStructures.hlsl"

Texture2D DiffuseMap : register(t0);
Texture2D NormalMap  : register(t1);
Texture2D HeightMap  : register(t2);

SamplerState samPointWrap        : register(s0);
SamplerState samPointClamp       : register(s1);
SamplerState samLinearWrap       : register(s2);
SamplerState samLinearClamp      : register(s3);
SamplerState samAnisotropicWrap  : register(s4);
SamplerState samAnisotropicClamp : register(s5);

ConstantBuffer<ObjectCB> cbObject : register(b0);

ConstantBuffer<MainPassCB> cbMainPass : register(b1);

ConstantBuffer<MaterialCB> cbMaterial : register(b2);

struct VS_INPUT
{
    float3 Pos : POSITION;
    float2 TexC : TEXCOORD;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
};

struct DS_VS_OUTPUT_PS_INPUT
{
    float4 PosCS : SV_POSITION;
    float3 PosW : POSITION;
    float2 TexC : TEXCOORD;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    //Jittered values
    float4 PrevPosCS : POSITION1;
    //We could just use PosCS but IT DOESN'T FUCKING WORK for some reason, instead of [-1, 1] it stays in [0, some big positive] and idk why
    float4 CurrPosCS : POSITION2;
    //UnJiterred values
    float4 PrevPosCSNoJitter : POSITION3;
    float4 CurrPosCSNoJitter : POSITION4;
};

DS_VS_OUTPUT_PS_INPUT VS(VS_INPUT vin)
{
    DS_VS_OUTPUT_PS_INPUT vout = (DS_VS_OUTPUT_PS_INPUT) 0.0f;
	
    float4 posW = mul(float4(vin.Pos, 1.0f), cbObject.World);
    vout.PosW = posW.xyz;

    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
    vout.Normal = normalize(mul(vin.Normal, (float3x3) cbObject.World));
    
    vout.Tangent = normalize(mul(vin.Tangent, (float3x3) cbObject.World));

    vout.PosCS = mul(posW, cbMainPass.ViewProj);
	
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), cbObject.TexTransform);
    vout.TexC = mul(texC, cbMaterial.MatTransform).xy;
    
    float4 prevPosW = mul(float4(vin.Pos, 1.0f), cbObject.PrevWorld);
    vout.PrevPosCS = mul(prevPosW, cbMainPass.PrevViewProj);
    
    vout.CurrPosCS = vout.PosCS;
    
    vout.CurrPosCSNoJitter = mul(posW, cbMainPass.ViewProjNoJitter);
    vout.PrevPosCSNoJitter = mul(prevPosW, cbMainPass.PrevViewProjNoJitter);
    return vout;
}

struct HS_CONSTANT_DATA_OUTPUT
{
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};


HS_CONSTANT_DATA_OUTPUT ConstantsHS(InputPatch<DS_VS_OUTPUT_PS_INPUT, 3> Patch, uint PatchID : SV_PrimitiveID)
{
    HS_CONSTANT_DATA_OUTPUT Out;
    
    // Backface Culling
    float3 vEdge0 = Patch[1].PosW - Patch[0].PosW;
    float3 vEdge2 = Patch[2].PosW - Patch[0].PosW;
    float3 vFaceNormal = normalize(cross(vEdge2, vEdge0));
    float3 vView = normalize(Patch[0].PosW - cbMainPass.CameraPos);
    
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
    Out.Edges[0] = cbObject.TesselationFactor;
    Out.Edges[1] = cbObject.TesselationFactor;
    Out.Edges[2] = cbObject.TesselationFactor;
    Out.Inside = cbObject.TesselationFactor;
    return Out;
}

struct HS_CONTROL_POINT_OUTPUT
{
    float3 vWorldPos : POSITION;
    float2 vTexCoord : TEXCOORD;
    float3 vNormal : NORMAL;
    float3 vTangent : TANGENT;
    float4 vPrevPosCS : POSITION1;
    float4 vCurrPosCS : POSITION2;
    float4 vPrevPosCSNJ : POSITION3;
    float4 vCurrPosCSNJ : POSITION4;
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
    Out.vPrevPosCS = inputPatch[uCPID].PrevPosCS;
    Out.vCurrPosCS = inputPatch[uCPID].CurrPosCS;
    Out.vPrevPosCSNJ = inputPatch[uCPID].PrevPosCSNoJitter;
    Out.vCurrPosCSNJ = inputPatch[uCPID].CurrPosCSNoJitter;
    return Out;
}

// Called once per tessellated vertex
[domain("tri")] // indicates that triangle patches were used
// The original patch is passed in, along with the vertex position in barycentric coordinates, and the patch constant phase hull shader output(tessellation factors)
DS_VS_OUTPUT_PS_INPUT DSMain(HS_CONSTANT_DATA_OUTPUT input, float3 BarycentricCoordinates : SV_DomainLocation, const OutputPatch<HS_CONTROL_POINT_OUTPUT, 3> TrianglePatch)
{
    DS_VS_OUTPUT_PS_INPUT Out;
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
    
    Out.Tangent =
    BarycentricCoordinates.x * TrianglePatch[0].vTangent +
    BarycentricCoordinates.y * TrianglePatch[1].vTangent +
    BarycentricCoordinates.z * TrianglePatch[2].vTangent;
    
    Out.PrevPosCS =
    BarycentricCoordinates.x * TrianglePatch[0].vPrevPosCS +
    BarycentricCoordinates.y * TrianglePatch[1].vPrevPosCS +
    BarycentricCoordinates.z * TrianglePatch[2].vPrevPosCS;
    
    Out.CurrPosCS =
    BarycentricCoordinates.x * TrianglePatch[0].vCurrPosCS +
    BarycentricCoordinates.y * TrianglePatch[1].vCurrPosCS +
    BarycentricCoordinates.z * TrianglePatch[2].vCurrPosCS;
    
    Out.PrevPosCSNoJitter =
    BarycentricCoordinates.x * TrianglePatch[0].vPrevPosCSNJ +
    BarycentricCoordinates.y * TrianglePatch[1].vPrevPosCSNJ +
    BarycentricCoordinates.z * TrianglePatch[2].vPrevPosCSNJ;
    
    Out.CurrPosCSNoJitter =
    BarycentricCoordinates.x * TrianglePatch[0].vCurrPosCSNJ +
    BarycentricCoordinates.y * TrianglePatch[1].vCurrPosCSNJ +
    BarycentricCoordinates.z * TrianglePatch[2].vCurrPosCSNJ;

    // sample the displacement map for the magnitude of displacement
    float fDisplacement = HeightMap.SampleLevel(samAnisotropicWrap, Out.TexC.xy, 0).r;

    float scale_x = length(float3(cbObject.TexTransform[0][0], cbObject.TexTransform[1][0], cbObject.TexTransform[2][0])); // TexScaleX
    float scale_y = length(float3(cbObject.TexTransform[0][1], cbObject.TexTransform[1][1], cbObject.TexTransform[2][1])); // TexScaleY
 
    fDisplacement *= (2.5f / (scale_x + scale_y));
    
    float3 vDirection = normalize(Out.Normal);
    // translate the position
    Out.PosW += vDirection * fDisplacement;
    // transform to clip space
    Out.PosCS = mul(float4(Out.PosW.xyz, 1), cbMainPass.ViewProj);
    return Out;
}

GBufferData PS(DS_VS_OUTPUT_PS_INPUT pin)
{
    
    GBufferData pout;
    
    float2 uv = pin.TexC;
    
#ifdef ROTATINGTILES 
    float2 tileid = floor(pin.TexC);
    float2 localuv = frac(pin.TexC) - 0.5;
    float parity = fmod(tileid.x + tileid.y, 2.0);
    float angle = (parity == 0) ? -cbMainPass.TotalTime : cbMainPass.TotalTime;
    float2 rotateduv;
    rotateduv.x = localuv.x * cos(angle) - localuv.y * sin(angle);
    rotateduv.y = localuv.x * sin(angle) + localuv.y * cos(angle);
    rotateduv += 0.5;
    uv = rotateduv;
#endif
    
    float3 NormalMapSample = NormalMap.Sample(samAnisotropicWrap, uv).rgb;
    float3 WorldNormal;
    if (!length(NormalMapSample) == 0.f)
    {
        float3 UnpackedNormal = NormalMapSample * 2.f - 1.f;
    // TBN
        float3 N = normalize(pin.Normal);
        float3 T = normalize(pin.Tangent);
        T = normalize(T - dot(T, N) * N);
        float3 B = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
    
    // TangentSpace to WorldSpace
        WorldNormal = normalize(mul(UnpackedNormal, TBN));
    }
    else
        WorldNormal = normalize(pin.Normal);
    
    float4 diffuseAlbedo = DiffuseMap.Sample(samAnisotropicWrap, uv);
    
    float2 currentNDC = pin.CurrPosCSNoJitter.xy / pin.CurrPosCSNoJitter.w;
    float2 prevNDC = pin.PrevPosCSNoJitter.xy / pin.PrevPosCSNoJitter.w;
    currentNDC = currentNDC * 0.5f + 0.5f;
    prevNDC = prevNDC * 0.5f + 0.5f;
    
    pout.diffuse = diffuseAlbedo;
    pout.emissive = float4(0.f, 0.f, 0.f, pin.PosCS.z); //xyz is free for now
    pout.normal = float4(WorldNormal, cbMaterial.Metallic);
    pout.MaterialFresnelRoughness = float4(cbMaterial.FresnelR0, cbMaterial.Roughness);
    pout.MotionVector = (prevNDC - currentNDC) * cbMainPass.RenderTargetSize;
    pout.MotionVector.y *= -1.f;
    
    //filter out MV noise
    if (length(pout.MotionVector) < 0.05)
        pout.MotionVector = float2(0.f, 0.f);
    
    return pout;
}
