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
    
    rayQuery.TraceRayInline(TLAS, 0x01, 0x01, ray);
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
    
    float2 UV = pin.PosH.xy / cbMainPass.RenderTargetSize;
    float screenDepth = DepthMaps.Load(int3(pin.PosH.xy, 0)).r;
    float3 WorldPos = ReconstructWorldPosition(UV, screenDepth);
    float3 Normal = NormalMap.Load(int3(pin.PosH.xy, 0)).rgb;
    
    if (screenDepth >= 1.0f)
    {
        res.Depth = 1.0f;
        return res;
    }
    
    float shadowFactor = 1.0f;
    
    // RayTrace based on light type
    if (cbLight.lightData.LightType == 0) // Directional Light
    {
        float3 RayOrigin = WorldPos + Normal * 0.1f;
        float3 lightDir = normalize(-cbLight.lightData.Direction);
        
        bool hit = TraceRay(RayOrigin, lightDir, 1000.0f);
        shadowFactor = hit ? 0.0f : 1.0f;
    }
    else if (cbLight.lightData.LightType == 1) // Point Light
    {
        float3 toLight = cbLight.lightData.Position - WorldPos;
        float distanceToLight = length(toLight);
        float3 lightDir = toLight / distanceToLight;
        
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