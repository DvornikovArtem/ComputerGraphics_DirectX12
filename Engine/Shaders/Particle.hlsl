#include "CBufferStructures.hlsl"

ConstantBuffer<MainPassCB> cbMainPass : register(b0);

StructuredBuffer<Particle> ParticlePool : register(t0);
StructuredBuffer<uint> AliveList : register(t1);

struct VS_INPUT
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VS_OUTPUT_PS_INPUT
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};

VS_OUTPUT_PS_INPUT VS(VS_INPUT input, uint instanceID : SV_InstanceID)
{
    VS_OUTPUT_PS_INPUT output;

    uint particleIndex = AliveList[instanceID];
    Particle p = ParticlePool[particleIndex];

    float3 particlePosW = p.Pos;
    float2 quadPosL = input.PosL.xy;
    
#ifdef BILLBOARDGEOMETRY
    float3 camRightW = cbMainPass.InvView[0].xyz;
    float3 camUpW = cbMainPass.InvView[1].xyz;
    particlePosW += camRightW * quadPosL.x * p.Size;
    particlePosW += camUpW * quadPosL.y * p.Size;
#else
    particlePosW += input.PosL * p.Size;
#endif
    
    output.PosH = mul(float4(particlePosW, 1.0f), cbMainPass.ViewProj);
    
    output.Color = p.Color;
    output.Color.a *= saturate(p.LifeTime / 2.0f);
    output.TexCoord = input.TexC;

    return output;
}


float4 PS(VS_OUTPUT_PS_INPUT input) : SV_TARGET
{
    return input.Color;
}