struct Particle
{
    float3 Pos;
    float LifeTime;
    float3 Vel;
    float Size;
    float4 Color;
};

// Используем тот же буфер констант, что и в симуляции
cbuffer ParticleConstants : register(b0)
{
    float3 gEmitterPos;
    float gDeltaTime;
    uint gNumEmit;
    uint gCurrentDeadList;
    uint gMaxParticles;
    float particleSize;
    float gTime;
    uint gFrameIndex;
    float3 CameraPos; // Нам нужна позиция камеры
    float3 CameraDir; // и направление
    float4 _pad;
};

// gParticlePool будет SRV, привязанным через таблицу дескрипторов, а не напрямую
//StructuredBuffer<Particle> gParticlePool : register(t0);
RWStructuredBuffer<Particle> gParticlePool : register(u0); // совпадает с UAV-таблицей u0

// gAliveList - это UAV, и он привязан к регистру u3 в C++ коде
RWStructuredBuffer<uint> gAliveList : register(u3);


groupshared uint sKeys[256]; // depth-key
groupshared uint sVals[256]; // particle index

// Вспомогательная функция для ключа сортировки
float DepthKey(float3 p)
{
    // Отрицательный — чтобы дальние (большой depth) шли первыми в списке
    return -dot(p - CameraPos, CameraDir);
}

[numthreads(256, 1, 1)]
void CS(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    // Индекс в глобальном списке gAliveList
    uint idx = dtid.x;
    
    // Получаем текущее количество живых частиц из счетчика UAV
    // Мы не можем сделать это напрямую в HLSL, поэтому диспетчеризация
    // должна быть выполнена для gMaxParticles, а внутри мы должны
    // быть осторожны. В данном случае, мы просто сортируем весь буфер.
    // Это не идеально, но для начала сойдет.

    uint pIndex = gAliveList[idx];
    float key = DepthKey(gParticlePool[pIndex].Pos);

    sKeys[groupIndex] = asuint(key);
    sVals[groupIndex] = pIndex;

    GroupMemoryBarrierWithGroupSync();

    // BITONIC SORT внутри группы (256 элементов)
    for (uint size = 2; size <= 256; size <<= 1)
    {
        // Определяем направление сортировки для этого потока
        uint dir = ((groupIndex & (size >> 1)) != 0);

        for (uint stride = size >> 1; stride > 0; stride >>= 1)
        {
            uint pos = groupIndex;
            uint peer = pos ^ stride;

            uint keyA = sKeys[pos];
            uint keyB = sKeys[peer];
            uint valA = sVals[pos];
            uint valB = sVals[peer];

            // Сравниваем и меняем местами, если нужно
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
    
    // Записываем отсортированное значение обратно в глобальную память
    gAliveList[idx] = sVals[groupIndex];
}