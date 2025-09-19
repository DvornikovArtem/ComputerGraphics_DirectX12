struct Particle
{
    float3 Pos;
    float LifeTime;
    float3 Vel;
    float Size;
    float4 Color;
};


struct VSInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};


cbuffer PassConstants : register(b0)
{
    matrix View;
    matrix InvView;
    matrix Proj;
    matrix InvProj;
    matrix ViewProj;
    matrix InvViewProj;
    float3 EyePosW;
    float cbPerObjectPad1;
    float2 RenderTargetSize;
    float2 InvRenderTargetSize;
    float NearZ;
    float FarZ;
    float TotalTime;
    float DeltaTime;
    float4 AmbientLight;
    float4 FogColor;
    float gFogStart;
    float gFogRange;
    float2 cbPerObjectPad2;
};


StructuredBuffer<Particle> gParticlePool : register(t0);
StructuredBuffer<uint> gAliveList : register(t1);


VSOutput VS(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput output;

    uint particleIndex = gAliveList[instanceID];
    Particle p = gParticlePool[particleIndex];

    float3 particlePosW = p.Pos;
    float2 quadPosL = input.PosL.xy;
    
    float3 camRightW = InvView[0].xyz;
    float3 camUpW = InvView[1].xyz;
    
    float3 worldPos = particlePosW;
    worldPos += camRightW * quadPosL.x * p.Size;
    worldPos += camUpW * quadPosL.y * p.Size;
    
    output.PosH = mul(float4(worldPos, 1.0f), ViewProj);
    
    output.Color = p.Color;
    output.Color.a *= saturate(p.LifeTime / 2.0f);
    output.TexCoord = input.TexC;

    return output;
}


float4 PS(VSOutput input) : SV_TARGET
{
    return input.Color;
}