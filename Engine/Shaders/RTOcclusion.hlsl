#include "CBufferStructures.hlsl"

RaytracingAccelerationStructure TLAS : register(t0);
RWStructuredBuffer<uint> VisibleObjectBuffer : register(u0);
ConstantBuffer<MainPassCB> cbMainPass : register(b0);

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

int TraceRay(float3 origin, float3 direction, float maxDistance)
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
    
    //if we hit something - return instance ID
    if (rayQuery.CommittedStatus() != COMMITTED_NOTHING)
    {
        return rayQuery.CommittedInstanceID();
    }
    // else return impossible index
    return -1;
}

void PS(VertexOut pin)
{
    uint2 TexelCoord = uint2(pin.PosH.xy);
    
    if ((TexelCoord.x + TexelCoord.y) % 2 != 0)
    {
        discard; // skip ray trace is chess-like order
    }
    
    uint bufferIndex = TexelCoord.y * cbMainPass.RenderTargetSize.x + TexelCoord.x;
    float2 uv = (TexelCoord + 0.5) * cbMainPass.InvRenderTargetSize;
    
    float2 clipPos = uv * 2.0 - 1.0;
    clipPos.y = -clipPos.y;
    
    float4 viewSpaceRay = mul(float4(clipPos, 1.0, 1.0), cbMainPass.InvProj);
    viewSpaceRay.xyz /= viewSpaceRay.w;
    
    float3 rayDirection = normalize(mul(float4(viewSpaceRay.xyz, 0.0), cbMainPass.InvView).xyz);
    
    float3 rayOrigin = cbMainPass.CameraPos;
    float maxDistance = cbMainPass.FarZ;
    int instanceID = TraceRay(rayOrigin, rayDirection, maxDistance);
    if (instanceID != -1)
    {
        VisibleObjectBuffer[instanceID] = 1;
    }
}