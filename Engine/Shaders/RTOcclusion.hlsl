#include "CBufferStructures.hlsl"

RaytracingAccelerationStructure TLAS : register(t0);
RWStructuredBuffer<uint> VisibleObjectBuffer : register(u0);
ConstantBuffer<MainPassCB> cbMainPass : register(b0);

struct [raypayload] RayPayload
{
    uint InstanceID : read(caller, closesthit, miss) : write(caller, closesthit, miss);
};

[shader("raygeneration")]
void RayGen()
{
    uint2 DispatchIndex = DispatchRaysIndex().xy;
    uint2 DispatchDimensions = DispatchRaysDimensions().xy;

    float2 uv = (DispatchIndex + 0.5) / DispatchDimensions;
    float2 clipPos = uv * 2.0 - 1.0;
    clipPos.y = -clipPos.y;

    float4 viewSpaceRay = mul(float4(clipPos, 1.0, 1.0), cbMainPass.InvProj);
    viewSpaceRay.xyz /= viewSpaceRay.w;
    float3 rayDirection = normalize(mul(float4(viewSpaceRay.xyz, 0.0), cbMainPass.InvView).xyz);
    float3 rayOrigin = cbMainPass.CameraPos;

    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDirection;
    ray.TMin = 0.001f;
    ray.TMax = cbMainPass.FarZ;

    RayPayload payload;
    payload.InstanceID = 0xFFFFFFFF;

    TraceRay(
        TLAS,
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF, 0, 0, 0,
        ray,
        payload);

    if (payload.InstanceID != 0xFFFFFFFF)
    {
        VisibleObjectBuffer[payload.InstanceID] = 1;
    }
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.InstanceID = InstanceID();
}

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
    payload.InstanceID = 0xFFFFFFFF;
}