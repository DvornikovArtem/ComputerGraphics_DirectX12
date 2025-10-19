// DeferredGeometryPass_Voxel.hlsl

#include "CBufferStructures.hlsl"

ConstantBuffer<ObjectCB> cbObject : register(b0);
ConstantBuffer<MainPassCB> cbMainPass : register(b1);
ConstantBuffer<MaterialCB> cbMaterial : register(b2);

Texture2D gTexX_Albedo : register(t0);
Texture2D gTexY_Albedo : register(t1);
Texture2D gTexZ_Albedo : register(t2);
SamplerState gSamp : register(s0);

struct VSIn
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 TexC : TEXCOORD0;
    float3 Tangent : TANGENT;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float3 WPos : TEXCOORD0;
    float3 WNorm : TEXCOORD1;
    float4 PrevPosH : TEXCOORD2;
};

VSOut VS(VSIn vin){
    VSOut o;
    //float4 wp = mul(float4(vin.Pos, 1), cbObject.World);
    float4 wp = float4(vin.Pos, 1.0f);
    o.WPos = wp.xyz;
    //float3 wn = mul(float4(vin.Normal, 0), cbObject.World).xyz;
    //o.WNorm = normalize(wn);
    o.WNorm = normalize(vin.Normal);
    o.PosH = mul(wp, cbMainPass.ViewProj);
    //o.PrevPosH = mul(mul(float4(vin.Pos, 1.0f), cbObject.PrevWorld), cbMainPass.PrevViewProj);
    o.PrevPosH = mul(wp, cbMainPass.PrevViewProj);
    return o;
}

float3 BlendWeights(float3 n){
    n = abs(n); float3 w = max(n, 1e-4); return w / (w.x + w.y + w.z);
}

float3 SampleTriplanarAlbedo(float3 wp, float3 wn, float texScale){
    float3 w = BlendWeights(wn);
    float2 uX = wp.zy * texScale;
    float2 uY = wp.xz * texScale;
    float2 uZ = wp.xy * texScale;
    float3 cx = gTexX_Albedo.Sample(gSamp, uX).rgb;
    float3 cy = gTexY_Albedo.Sample(gSamp, uY).rgb;
    float3 cz = gTexZ_Albedo.Sample(gSamp, uZ).rgb;
    return cx*w.x + cy*w.y + cz*w.z;
}

struct GBufferOut
{
    float4 RT0 : SV_Target0; // Diffuse
    float4 RT1 : SV_Target1; // Emissive
    float4 RT2 : SV_Target2; // Normal
    float4 RT3 : SV_Target3; // Material Albedo
    float4 RT4 : SV_Target4; // Material Fresnel/Roughness
    float2 RT5 : SV_Target5; // Velocity
};

GBufferOut PS(VSOut pin){
    GBufferOut o;
    float3 N = normalize(pin.WNorm);
    //float3 albedo = SampleTriplanarAlbedo(pin.WPos, N, TexScale) * BaseColor.rgb;
    float3 albedo = SampleTriplanarAlbedo(pin.WPos, N, 1.0f) * cbMaterial.DiffuseAlbedo.rgb;
    o.RT0 = float4(albedo,1);
    o.RT1 = 0;
    //o.RT2 = float4(N*0.5+0.5,1); // if NormalRT is SNORM you'll clamp/convert in RTV
    o.RT2 = float4(N, 1.0f);
    o.RT3 = float4(albedo,1);
    //o.RT4 = float4(0.04, 0.5, 0, 1);
    o.RT4 = float4(cbMaterial.FresnelR0, cbMaterial.Roughness);
    float2 currNDC = pin.PosH.xy / pin.PosH.w;
    float2 prevNDC = pin.PrevPosH.xy / pin.PrevPosH.w;
    float2 velocity = currNDC - prevNDC;
    o.RT5 = 0;
    return o;
}