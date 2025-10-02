#include "LightingUtil.hlsl"

struct MainPassCB
{
    float4x4 View;
    float4x4 InvView;
    float4x4 Proj;
    float4x4 InvProj;
    float4x4 ViewProj;
    float4x4 InvViewProj;
    float3 CameraPos;
    float _pad0;
    float2 RenderTargetSize;
    float2 InvRenderTargetSize;
    float NearZ;
    float FarZ;
    float TotalTime;
    float DeltaTime;
    float4 AmbientLight;
    float3 PrevCameraPos;
    float _pad1;
    float4x4 PrevViewProj;
    float _pad2;
    float3 CameraDirection;
};

struct ObjectCB
{
    float4x4 World;
    float4x4 TexTransform;
    float TesselationFactor;
    float HeightMapScale;
    float2 _pad0;
    float4x4 PrevWorld;
};

struct MaterialCB
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float Metallic;
    float _pad1;
    float _pad2;
    float _pad3;
    float4x4 MatTransform;
};

struct LightCB
{
    Light lightData;
    float4x4 World;
    float4x4 View[6];
    float4x4 Proj[6];
    float4x4 ShadowTransform[6];
    float4 CascadeDistances;
};

struct GBufferData
{
    float4 diffuse : SV_TARGET0;
    float4 emissive : SV_TARGET1;
    float4 normal : SV_TARGET2;
    float4 materialAlbedo : SV_TARGET3;
    float4 MaterialFresnelRoughness : SV_TARGET4;
    float2 MotionVector : SV_TARGET5;
};

struct Particle
{
    float3 Pos;
    float LifeTime;
    float3 Velocity;
    float Size;
    float4 Color;
};

struct ParticleCB
{
    float3 EmitterPos;
    float DeltaTime;
    uint NumEmit;
    uint CurrentDeadList;
    uint MaxParticles;
    float ParticleSize;
    float Time;
    uint FrameIndex;
    float3 CameraPos;
    float3 CameraDir;
    float4 _pad;
};