// Сначала определяем структуры
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

// Потом константы и ресурсы
cbuffer PassConstants : register(b0)
{
    matrix viewProj;
};

StructuredBuffer<Particle> gParticlePool : register(t0);
StructuredBuffer<uint> gAliveList : register(t1); // Теперь это SRV

VSOutput VS(VSInput input, uint instanceID : SV_InstanceID)
{
    // ... (остальной код VS и PS без изменений)
    VSOutput output;

    uint particleIndex = gAliveList[instanceID];
    Particle p = gParticlePool[particleIndex];

    float3 particlePosW = p.Pos;
    float3 quadPosL = input.PosL;

    // TODO: Правильное создание билборда
    float3 worldPos = particlePosW + quadPosL * p.Size;

    output.PosH = mul(float4(worldPos, 1.0f), viewProj);
    output.Color = p.Color;
    output.Color.a *= saturate(p.LifeTime / 2.0f); // Нормализуем альфу
    output.TexCoord = input.TexC;

    return output;
}

float4 PS(VSOutput input) : SV_TARGET
{
    return input.Color;
}