struct Particle
{
    float3 Pos;
    float LifeTime;
    float3 Vel;
    float Size;
    float4 Color;
};

cbuffer ParticleConstants : register(b0)
{
    float3 gEmitterPos;
    float gDeltaTime;
    uint gNumEmit;
    uint gCurrentDeadList;
    uint gMaxParticles;
    float Pad;
};


RWStructuredBuffer<Particle> gParticlePool : register(u0);

ConsumeStructuredBuffer<uint> gDeadListsConsume[2] : register(u1); // u1, u2
AppendStructuredBuffer<uint> gDeadListsAppend[2] : register(u1); // u1, u2

AppendStructuredBuffer<uint> gAliveListAppend : register(u3);
RWByteAddressBuffer gDrawArgs : register(u4);


float rand_float(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return (float) seed / 4294967295.0f;
}


[numthreads(256, 1, 1)]
void EmitCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= gNumEmit)
        return;

    uint deadIndex = gDeadListsConsume[gCurrentDeadList].Consume();
    uint seed = deadIndex + (uint) (gDeltaTime * 1000.0f);

    gParticlePool[deadIndex].Pos = gEmitterPos;
    gParticlePool[deadIndex].LifeTime = 2.0f + rand_float(seed++) * 2.0f;
    gParticlePool[deadIndex].Vel = float3(
        rand_float(seed++) * 2.0f - 1.0f, // x [-1, 1]
        2.0f + rand_float(seed++) * 3.0f, // y [2, 5]
        rand_float(seed++) * 2.0f - 1.0f // z [-1, 1]
    ) * 2.0f;
    //gParticlePool[deadIndex].Size = 0.2f + rand_float(seed) * 0.3f;
    gParticlePool[deadIndex].Size = 0.2f;
    gParticlePool[deadIndex].Color = float4(1.0f, 0.5f, 0.1f, 1.0f);
}


[numthreads(256, 1, 1)]
void SimulateCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint index = dispatchThreadID.x;
    if (index >= gMaxParticles)
        return;

    Particle p = gParticlePool[index];

    if (p.LifeTime > 0.0f)
    {
        p.LifeTime -= gDeltaTime;

        if (p.LifeTime > 0.0f)
        {
            p.Vel.y -= 3.8f * gDeltaTime;
            p.Pos += p.Vel * gDeltaTime;
            gAliveListAppend.Append(index);
        }
        else
        {
            gDeadListsAppend[1 - gCurrentDeadList].Append(index);
        }
        
        gParticlePool[index] = p;
    }
    else
    {
        gDeadListsAppend[1 - gCurrentDeadList].Append(index);
    }
}