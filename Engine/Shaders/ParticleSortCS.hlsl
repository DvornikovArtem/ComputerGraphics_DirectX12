#include "CBufferStructures.hlsl"

ConstantBuffer<ParticleCB> cbParticle : register(b0);

RWStructuredBuffer<Particle> ParticlePool : register(u0); 

RWStructuredBuffer<uint> AliveList : register(u3);


groupshared uint sKeys[256]; // depth-key
groupshared uint sVals[256]; // particle index

float DepthKey(float3 p)
{
    return -dot(p - cbParticle.CameraPos, cbParticle.CameraDir);
}

[numthreads(256, 1, 1)]
void CS(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    uint idx = dtid.x;
    
    // Получаем текущее количество живых частиц из счетчика UAV
    // Мы не можем сделать это напрямую в HLSL, поэтому диспетчеризация
    // должна быть выполнена для gMaxParticles, а внутри мы должны
    // быть осторожны. В данном случае, мы просто сортируем весь буфер.
    // Это не идеально, но для начала сойдет.

    uint pIndex = AliveList[idx];
    float key = DepthKey(ParticlePool[pIndex].Pos);

    sKeys[groupIndex] = asuint(key);
    sVals[groupIndex] = pIndex;

    GroupMemoryBarrierWithGroupSync();

    // BITONIC SORT
    for (uint size = 2; size <= 256; size <<= 1)
    {
        uint dir = ((groupIndex & (size >> 1)) != 0);

        for (uint stride = size >> 1; stride > 0; stride >>= 1)
        {
            uint pos = groupIndex;
            uint peer = pos ^ stride;

            uint keyA = sKeys[pos];
            uint keyB = sKeys[peer];
            uint valA = sVals[pos];
            uint valB = sVals[peer];

            bool swap = ((keyA > keyB) == dir);
            if (swap)
            {
                sKeys[pos] = keyB;
                sKeys[peer] = keyA;
                sVals[pos] = valB;
                sVals[peer] = valA;
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }
    
    AliveList[idx] = sVals[groupIndex];
}