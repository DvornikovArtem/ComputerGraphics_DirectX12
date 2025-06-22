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
    float particleSize;
    float gTime;
    uint gFrameIndex;
    float2 _pad;
};

cbuffer PassConstants : register(b1)
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

RWStructuredBuffer<Particle> gParticlePool : register(u0);

ConsumeStructuredBuffer<uint> gDeadListsConsume[2] : register(u1); // u1, u2
AppendStructuredBuffer<uint> gDeadListsAppend[2] : register(u1); // u1, u2

AppendStructuredBuffer<uint> gAliveListAppend : register(u3);
RWByteAddressBuffer gDrawArgs : register(u4);

Texture2D gEmissiveMap : register(t0);


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
    gParticlePool[deadIndex].Size = particleSize;
    gParticlePool[deadIndex].Color = float4(1.0f, 0.5f, 0.1f, 1.0f);
}


[numthreads(256, 1, 1)]
void SimulateCS2(uint3 dispatchThreadID : SV_DispatchThreadID)
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


// Хелпер-функция для преобразования глубины из пространства экрана в позицию в мировом пространстве
float3 Unproject(float3 screenPos)
{
    // screenPos.xy - это координаты текселя [0..width, 0..height]
    // screenPos.z - это значение глубины [0..1]

    // Преобразуем в NDC [-1..1]
    float2 ndc = (screenPos.xy * InvRenderTargetSize) * 2.0f - 1.0f;
    ndc.y = -ndc.y; // Инвертируем Y для D3D

    // Преобразуем в мировые координаты
    float4 p = mul(float4(ndc, screenPos.z, 1.0f), InvViewProj);
    return p.xyz / p.w;
}


// Пример для SimulateCS (фейерверк)
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
            // Симуляция
            p.Vel.y -= 3.8f * gDeltaTime;
            float3 nextPos = p.Pos + p.Vel * gDeltaTime;

            // --- Проверка столкновений ---
            float4 posH = mul(float4(nextPos, 1.0f), ViewProj);
            posH.xyz /= posH.w; // Perspective divide

            // Проверяем, находится ли частица в пределах экрана
            if (saturate(posH.x) == posH.x && saturate(posH.y) == posH.y)
            {
                // Конвертируем NDC в UV координаты
                float2 texCoord = 0.5f * posH.xy + 0.5f;
                texCoord.y = 1.0f - texCoord.y;

                uint2 screenPos = texCoord * RenderTargetSize;
                float sceneDepth = gEmissiveMap.Load(int3(screenPos, 0)).w;

                // Если глубина сцены не максимальна (т.е. там есть геометрия)
                if (sceneDepth < 1.0f)
                {
                    float3 sceneWorldPos = Unproject(float3(screenPos, sceneDepth));

                    // Если частица "за" поверхностью
                    if (length(nextPos - EyePosW) > length(sceneWorldPos - EyePosW))
                    {
                        // Простая реакция - отскок от пола
                        p.Vel.y = -p.Vel.y * 0.4f; // Теряем часть энергии
                        nextPos = p.Pos + p.Vel * gDeltaTime;
                    }
                }
            }
            // Простое столкновение с полом на y=0
            if (nextPos.y < 0.0f)
            {
                nextPos.y = 0.0f;
                p.Vel.y = -p.Vel.y * 0.4f;
            }
            p.Pos = nextPos;
            // --- Конец проверки столкновений ---

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


uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

float hash11(float p)
{
    return hash(asuint(p)) / 4294967296.0;
}
float hash13(float3 p)
{
    return hash(asuint(p.x) ^ hash(asuint(p.y)) ^ hash(asuint(p.z))) / 4294967296.0;
}


float noise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash13(i + float3(0, 0, 0));
    float n100 = hash13(i + float3(1, 0, 0));
    float n010 = hash13(i + float3(0, 1, 0));
    float n110 = hash13(i + float3(1, 1, 0));
    float n001 = hash13(i + float3(0, 0, 1));
    float n101 = hash13(i + float3(1, 0, 1));
    float n011 = hash13(i + float3(0, 1, 1));
    float n111 = hash13(i + float3(1, 1, 1));

    float n00 = lerp(n000, n100, f.x);
    float n10 = lerp(n010, n110, f.x);
    float n01 = lerp(n001, n101, f.x);
    float n11 = lerp(n011, n111, f.x);

    float n0 = lerp(n00, n10, f.y);
    float n1 = lerp(n01, n11, f.y);

    return lerp(n0, n1, f.z);
}

float3 curlNoise(float3 p)
{
    const float e = 0.1;
    float nx = noise(p + float3(e, 0, 0)) - noise(p - float3(e, 0, 0));
    float ny = noise(p + float3(0, e, 0)) - noise(p - float3(0, e, 0));
    float nz = noise(p + float3(0, 0, e)) - noise(p - float3(0, 0, e));

    return normalize(float3(nz - ny, nx - nz, ny - nx));
}


[numthreads(256, 1, 1)]
void EmitSmokeCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= gNumEmit)
        return;

    uint slot = gDeadListsConsume[gCurrentDeadList].Consume();
    
    uint seed = slot * 9781 + gFrameIndex;
    
    float3 offset = (float3(hash11(seed++), hash11(seed++), hash11(seed++)) - 0.5) * float3(0.8, 0.2, 0.8);
    gParticlePool[slot].Pos = gEmitterPos + offset;
    
    gParticlePool[slot].Vel = float3(
        (hash11(seed++) - 0.5) * 0.5,
         10.0 + hash11(seed++) * 0.4,
        (hash11(seed++) - 0.5) * 0.5);

    gParticlePool[slot].LifeTime = 5.0 + hash11(seed++) * 1.5;
    gParticlePool[slot].Size = particleSize * (0.6 + hash11(seed++) * 0.4);
    gParticlePool[slot].Color = float4(0.06, 0.06, 0.06, 0.85);
}



// Вставьте этот код в ParticleCS.hlsl, полностью заменив старую функцию SimulateSmokeCS

[numthreads(256, 1, 1)]
void SimulateSmokeCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint idx = dispatchThreadID.x;
    if (idx >= gMaxParticles)
        return;

    Particle p = gParticlePool[idx];

    // Если частица еще жива, симулируем ее
    if (p.LifeTime > 0.0f)
    {
        p.LifeTime -= gDeltaTime;

        // Если частица все еще жива после обновления
        if (p.LifeTime > 0.0f)
        {
            float3 buoyancy = float3(0.0, 1.1, 0.0);
            float3 windDir = float3(0.25, 0.0, 0.05);
            p.Vel += (buoyancy + windDir) * gDeltaTime;

            float3 field = curlNoise(p.Pos * 0.25 + gTime * 0.5);
            p.Vel += field * 0.8 * gDeltaTime;

            // Затухание скорости (сопротивление воздуха)
            p.Vel *= 0.96;
            p.Pos += p.Vel * gDeltaTime;

            // Частица растет со временем
            p.Size += particleSize * 0.7 * gDeltaTime;

            // Плавное появление и исчезновение
            float t_fade = p.LifeTime / 5.0f; // Предполагаем, что начальное время жизни около 5.0
            p.Color.a = saturate(1.0 - (1.0 - t_fade) * (1.0 - t_fade)) * 0.8f;

            // Цвет меняется от плотного серого к более светлому и прозрачному
            p.Color.rgb = lerp(float3(0.4, 0.4, 0.4), float3(0.15, 0.15, 0.15), saturate(t_fade));

            gParticlePool[idx] = p;
            gAliveListAppend.Append(idx);
        }
        else // Частица умерла в этом кадре
        {
            gDeadListsAppend[1 - gCurrentDeadList].Append(idx);
        }
    }
    else // Частица уже была мертва
    {
        gDeadListsAppend[1 - gCurrentDeadList].Append(idx);
    }
}