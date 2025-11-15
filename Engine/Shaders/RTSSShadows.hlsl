#include "CBufferStructures.hlsl"

RaytracingAccelerationStructure TLAS : register(t0);
Texture2D DepthMaps : register(t1);
Texture2D NormalMap : register(t2);

ConstantBuffer<MainPassCB> cbMainPass : register(b0);
ConstantBuffer<LightCB> cbLight : register(b1);

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
    
    vout.PosH = mul(float4(vin.PosL, 1.0f), mul(cbLight.World, cbMainPass.ViewProj));
    
    return vout;
}

bool TraceRay(float3 origin, float3 direction, float maxDistance)
{
    RayQuery < RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH > rayQuery;
    
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = normalize(direction);
    ray.TMin = 0;
    ray.TMax = maxDistance;
    
    rayQuery.TraceRayInline(TLAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xff, ray);
    rayQuery.Proceed();
    
    return rayQuery.CommittedStatus() != COMMITTED_NOTHING;
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
    float4 viewPos = mul(clipPos, cbMainPass.InvViewProj);
    viewPos.xyz /= viewPos.w;

    return viewPos.xyz;
}

struct psout
{
    float Depth : SV_Depth;
};

psout PS(VertexOut pin)
{
    psout res;
    
    float2 UV = pin.PosH.xy / cbMainPass.ViewportSize;
    float screenDepth = DepthMaps.Load(int3(pin.PosH.xy, 0)).w;
    float3 WorldPos = ReconstructWorldPosition(UV, screenDepth);
    float3 Normal = NormalMap.Load(int3(pin.PosH.xy, 0)).rgb;
    
    if (screenDepth >= 1.0f)
    {
        res.Depth = 1.0f;
        return res;
    }
    
    float shadowFactor = 1.0f;
    float rayJitter = 0.01f;
    
    // RayTrace based on light type
    if (cbLight.lightData.LightType == 0) // Directional Light
    {
        float3 RayOrigin = WorldPos + Normal * 0.1f;
        float3 lightDir = normalize(-cbLight.lightData.Direction + float3(
            (frac(sin(dot(UV, float2(12.9898, 78.233))) * 43758.5453) - 0.5f) * rayJitter,
            (frac(sin(dot(UV, float2(39.346, 11.135))) * 43758.5453) - 0.5f) * rayJitter,
            (frac(sin(dot(UV, float2(67.89, 45.321))) * 43758.5453) - 0.5f) * rayJitter
        ));
        
        bool hit = TraceRay(RayOrigin, lightDir, 1000.0f);
        shadowFactor = hit ? 0.0f : 1.0f;
    }
    else if (cbLight.lightData.LightType == 1) // Point Light
    {
        float3 toLight = cbLight.lightData.Position - WorldPos;
        float distanceToLight = length(toLight);
        float3 lightDir = normalize(toLight / distanceToLight + float3(
            (frac(sin(dot(UV, float2(23.456, 89.012))) * 43758.5453) - 0.5f) * rayJitter,
            (frac(sin(dot(UV, float2(56.789, 34.567))) * 43758.5453) - 0.5f) * rayJitter,
            (frac(sin(dot(UV, float2(90.123, 67.890))) * 43758.5453) - 0.5f) * rayJitter
        ));
        
        float3 rayOrigin = WorldPos + normalize(Normal) * 0.1f;
        
        bool hit = TraceRay(rayOrigin, lightDir, distanceToLight - 0.2f);
        shadowFactor = hit ? 0.0f : 1.0f;
    }
    else if (cbLight.lightData.LightType == 2) // Spot Light
    {
        float3 toLight = cbLight.lightData.Position - WorldPos;
        float distanceToLight = length(toLight);
        float3 lightDir = toLight / distanceToLight;
        
        float3 rayOrigin = WorldPos + Normal * 0.1f;
        
        bool hit = TraceRay(rayOrigin, lightDir, distanceToLight - 0.2f);
        shadowFactor = hit ? 0.0f : 1.0f;
    }
    
    //draw result into Depth stencil
    res.Depth = shadowFactor;
    return res;
}

psout PS_BlurPass(VertexOut pin)
{
    psout res;
    //blur screen depth map
    
    static const float Kernel[41] =
    {
        0.002216, 0.003266, 0.004700, 0.006582, 0.008956,
    0.011834, 0.015192, 0.018965, 0.023049, 0.027308,
    0.031580, 0.035684, 0.039434, 0.042651, 0.045175,
    0.046876, 0.047663, 0.047488, 0.046347, 0.044274,
    0.041341, 0.037648, 0.033315, 0.028478, 0.023284,
    0.017884, 0.012429, 0.008062, 0.004903, 0.002818,
    0.001519, 0.000765, 0.000359, 0.000156, 0.000062,
    0.000022, 0.000007, 0.000002, 0.000000, 0.000000,
    0.000000
    };
    
    uint width, height, numLayers;
    DepthMaps.GetDimensions(0, width, height, numLayers);
    float2 viewportSize = float2(width, height);
    float blurStrength = 1.0f;
    
    float result = 0.0f;
    float kernelSum = 0.0f;
    
    [unroll]
    for (int x = -20; x <= 20; x++)
    {
        [unroll]
        for (int y = -20; y <= 20; y++)
        {
            float2 offset = float2(x, y) / viewportSize * blurStrength;
            int2 sampleCoord = pin.PosH.xy + int2(offset * viewportSize);
            
            if (sampleCoord.x >= 0 && sampleCoord.x < width &&
                sampleCoord.y >= 0 && sampleCoord.y < height)
            {
                float kernelValue = Kernel[x + 20] * Kernel[y + 20];
                float sampleValue = DepthMaps.Load(int3(sampleCoord, 0)).x;
                result += sampleValue * kernelValue;
                kernelSum += kernelValue;
            }
        }
    }
    
    // Нормализация результата
    if (kernelSum > 0.0f)
    {
        result /= kernelSum;
    }
    
    res.Depth = result;
    return res;
}