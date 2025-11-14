#include "CBufferStructures.hlsl"

ConstantBuffer<ParticleCB> cbParticle : register(b0);

ConstantBuffer<MainPassCB> cbMainPass : register(b1);

RWStructuredBuffer<Particle> ParticlePool : register(u0);

ConsumeStructuredBuffer<uint> DeadListsConsume[2] : register(u1); // u1, u2
AppendStructuredBuffer<uint> DeadListsAppend[2] : register(u1); // u1, u2

AppendStructuredBuffer<uint> AliveListAppend : register(u3);
RWByteAddressBuffer DrawArgs : register(u4);

Texture2D DepthMaps : register(t0);
Texture2D NormalTex : register(t1);

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
    if (dispatchThreadID.x >= cbParticle.NumEmit)
        return;
    
    uint deadIndex = DeadListsConsume[cbParticle.CurrentDeadList].Consume();
    
    uint seed = deadIndex + (uint) (cbMainPass.DeltaTime * 1000.0f);

    float rand1 = rand_float(seed++);
    float rand2 = rand_float(seed++);
    float rand3 = rand_float(seed++);
    float rand4 = rand_float(seed++);
    
    ParticlePool[deadIndex].Pos = cbParticle.EmitterPos;
    ParticlePool[deadIndex].LifeTime = 2.0f + rand1 * 2.0f;
    ParticlePool[deadIndex].Velocity = float3(
        rand2 * 2.0f - 1.0f, // x [-1, 1]
        1.0f + rand3 * 3.0f, // y [2, 5]
        rand4 * 2.0f - 1.0f // z [-1, 1]
    ) * 2.0f;
    ParticlePool[deadIndex].Size = cbParticle.ParticleSize;
    
    float rand5 = rand_float(seed++);
    float rand6 = rand_float(seed++);
    float rand7 = rand_float(seed++);
    
    ParticlePool[deadIndex].Color = float4(rand5, rand6, rand7, 1.0f);
    
    
    {
        uint index = dispatchThreadID.x;
        Particle p = ParticlePool[index];
        float3 nextPos = p.Pos + p.Velocity * cbMainPass.DeltaTime;
            
        float4 posH = mul(float4(nextPos, 1.0f), cbMainPass.ViewProj);
        posH.xyz /= posH.w;
            
        float2 texCoord = 0.5f * posH.xy + 0.5f;
        texCoord.y = 1.0f - texCoord.y;

        uint2 screenPos = texCoord * cbMainPass.RenderTargetSize;
        
        if (DepthMaps.Load(int3(screenPos, 0)).w == 0)
            ParticlePool[deadIndex].Color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
}


[numthreads(256, 1, 1)]
void SimulateCS2(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint index = dispatchThreadID.x;
    if (index >= cbParticle.MaxParticles)
        return;

    Particle p = ParticlePool[index];

    if (p.LifeTime > 0.0f)
    {
        p.LifeTime -= cbMainPass.DeltaTime;

        if (p.LifeTime > 0.0f)
        {
            p.Velocity.y -= 3.8f * cbMainPass.DeltaTime;
            p.Pos += p.Velocity * cbMainPass.DeltaTime;
            AliveListAppend.Append(index);
        }
        else
        {
            DeadListsAppend[1 - cbParticle.CurrentDeadList].Append(index);
        }
        
        ParticlePool[index] = p;
    }
    else
    {
        DeadListsAppend[1 - cbParticle.CurrentDeadList].Append(index);
    }
}


float3 Unproject(float3 screenPos)
{
    float2 ndc = (screenPos.xy * cbMainPass.InvRenderTargetSize) * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    
    float4 p = mul(float4(ndc, screenPos.z, 1.0f), cbMainPass.InvViewProj);
    return p.xyz / p.w;
}


// -- Depth helpers --------------------------------------------------
float LinearizeDepth(float ndcDepth)
{
    return cbMainPass.NearZ * cbMainPass.FarZ / (cbMainPass.FarZ - ndcDepth * (cbMainPass.FarZ - cbMainPass.NearZ));
}

float GetSceneViewDepth(int2 pix)
{
    float ndc = DepthMaps.Load(int3(pix, 0)).w;
    return LinearizeDepth(ndc);
}

bool WorldToPixel(float3 pos, out int2 pix)
{
    float4 clip = mul(float4(pos, 1.0f), cbMainPass.ViewProj);
    if (abs(clip.w) < 1e-6f)
        return false;

    float3 ndc = clip.xyz / clip.w;
    if (abs(ndc.x) > 1.0f || abs(ndc.y) > 1.0f)
        return false;

    float2 uv = ndc.xy * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;
    pix = int2(uv * cbMainPass.RenderTargetSize + 0.5f);
    pix = clamp(pix, int2(0, 0), int2(cbMainPass.RenderTargetSize) - int2(1, 1));
    return true;
}


float3 FetchNormal(int2 pix)
{
    float3 enc = NormalTex.Load(int3(pix, 0)).xyz;
    
    float3 n = normalize(enc * 2.0f - 1.0f);
    

    return n;
}

float3 SceneWorld(int2 pix)
{
    float ndcDepth = DepthMaps.Load(int3(pix, 0)).w;
    float2 uv = (float2(pix) + 0.5f) / cbMainPass.RenderTargetSize;
    float4 clip = float4(uv * 2.0f - 1.0f, ndcDepth, 1.0f);
    float4 ws = mul(clip, cbMainPass.InvViewProj);
    return ws.xyz / ws.w;
}

float3 SceneNormal(int2 pix)
{
    float3 enc = NormalTex.Load(int3(pix, 0)).xyz; // [0..1]
    float3 nVS = normalize(enc * 2.0f - 1.0f); // view-space

    // world = transpose(View) * nVS
    float3x3 viewInvT = (float3x3) cbMainPass.View;
    return normalize(mul(nVS, viewInvT));
}

bool Bounce(inout float3 pos, inout float3 vel, int2 pix)
{
    float CollisionRestitution = 0.8f;
    float CollisionThreshold = 0.01f;
    
    float3 sPos = SceneWorld(pix);
    float3 n = SceneNormal(pix);
    
    if (dot(n, pos - sPos) < 0.0f)
        n = -n;
    
    float dist = dot(pos - sPos, n);
    if (dist >= 0.0f)
        return false;
    
    if (dot(vel, n) >= 0.0f)
        return false;
    
    vel = vel - (1.0f + CollisionRestitution) * dot(vel, n) * n;
    
    pos += n * (-dist + CollisionThreshold);

    return true;
}

bool DepthBounce(inout float3 pos, inout float3 vel, int2 pix)
{
    const float Restitution = 0.8f; // коэффициент упругости
    const float PushOut = 0.02f; // выталкиваем на 2 см

    // world-положение поверхности и нормаль
    float3 sPos = SceneWorld(pix);
    float3 n = SceneNormal(pix);

    // гарантируем, что n «смотрит» к частице
    if (dot(n, pos - sPos) < 0.0f)
        n = -n;

    // проникновение вдоль нормали
    float dist = dot(pos - sPos, n);

    // Нет проникновения нет отражения
    if (dist >= 0.0f || dot(vel, n) >= 0.0f)
        return false;

    // Отражаем скорость и выталкиваем
    vel = reflect(vel, n) * Restitution;
    pos -= n * (dist - PushOut);

    return true;
}

[numthreads(256, 1, 1)]
void SimulateCS(uint3 tid : SV_DispatchThreadID)
{
    float3 Gravity = { 0.0f, -9.8f, 0.0f };
    float CollisionRestitution = 0.8f;
    float CollisionThreshold = 0.1f;
    uint idx = tid.x;
    if (idx >= cbParticle.MaxParticles)
        return;

    Particle p = ParticlePool[idx];
    if (p.LifeTime <= 0.0f)
    {
        DeadListsAppend[1 - cbParticle.CurrentDeadList].Append(idx);
        return;
    }
    
    p.LifeTime -= cbMainPass.DeltaTime;
    
    const int kSteps = 2;
    const float dt = cbMainPass.DeltaTime / kSteps;

    for (int s = 0; s < kSteps; ++s)
    {
        p.Velocity += Gravity * dt;
        float3 nextPos = p.Pos + p.Velocity * dt;

        int2 pix;
        if (WorldToPixel(nextPos, pix))
            DepthBounce(nextPos, p.Velocity, pix); // НОВЫЙ вызов

        p.Pos = nextPos;
    }
    
    {
    // Проецируем позицию частицы
        float4 clip = mul(float4(p.Pos, 1.0f), cbMainPass.ViewProj);

    // Отбрасываем частицы за пределами вьюпорта
        if (abs(clip.w) > 1e-6f)
        {
            float3 ndc = clip.xyz / clip.w;
            if (abs(ndc.x) <= 1.0f && abs(ndc.y) <= 1.0f)
            {
            // Пиксель в render-таргете
                float2 uv = ndc.xy * 0.5f + 0.5f;
                uv.y = 1.0f - uv.y;
                int2 pix = int2(uv * cbMainPass.RenderTargetSize + 0.5f);

            // Глубина сцены (переводим в линейную!)
                float ndcScene = DepthMaps.Load(int3(pix, 0)).w;
                float sceneDepth = LinearizeDepth(ndcScene);

            // Глубина частицы
                float particleDepth = LinearizeDepth(ndc.z);

                if (particleDepth > sceneDepth)            // частица «позади» геометрии
                {
                    float3 n = SceneNormal(pix);
                    if (dot(n, p.Velocity) > 0.0f)
                        n = -n; // направляем к частице

                    p.Velocity = reflect(p.Velocity, n) * 2.f; // 0.8 = restitution
                }
            }
        }
    }
   
    
    ParticlePool[idx] = p;
    AliveListAppend.Append(idx);
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
    if (dispatchThreadID.x >= cbParticle.NumEmit)
        return;

    uint slot = DeadListsConsume[cbParticle.CurrentDeadList].Consume();
    
    uint seed = slot * 9781 + cbParticle.FrameIndex * cbParticle.NumEmit + dispatchThreadID.x;
    
    {
        uint s = seed;
        float rand1 = rand_float(s);
        s++;
        float rand2 = rand_float(s);
        s++;
        float rand3 = rand_float(s);
        s++;
        
        float3 rnd = float3(rand1, rand2, rand3);
        float3 offset = (rnd * 2.0f - 1.0f) * float3(0.1f, 0.4f, 0.2f);
        ParticlePool[slot].Pos = cbParticle.EmitterPos + offset;
    }
    
    //gParticlePool[slot].Pos = gEmitterPos + offset;
    
    float rand4 = hash11(seed);
    seed++;
    float rand5 = hash11(seed);
    seed++;
    float rand6 = hash11(seed);
    seed++;
    float rand7 = hash11(seed);
    seed++;
    float rand8 = hash11(seed);
    seed++;
    
    ParticlePool[slot].Velocity = float3(
        (rand4 - 0.5) * 0.5,
        2.0 + rand5 * 0.4,
        (rand6 - 0.5) * 0.5
    );

    ParticlePool[slot].LifeTime = 5.0 + rand7 * 1.5;
    ParticlePool[slot].Size = cbParticle.ParticleSize * (0.6 + rand8 * 0.4);
    ParticlePool[slot].Color = float4(0.06, 0.06, 0.06, 0.85);
}

[numthreads(256, 1, 1)]
void SimulateSmokeCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint idx = dispatchThreadID.x;
    if (idx >= cbParticle.MaxParticles)
        return;

    Particle p = ParticlePool[idx];
    
    if (p.LifeTime > 0.0f)
    {
        p.LifeTime -= cbMainPass.DeltaTime;
        
        if (p.LifeTime > 0.0f)
        {
            float3 buoyancy = float3(0.0, 0.3, 0.0);
            //float3 windDir = float3(0.25, 0.0, 0.05);
            //p.Vel += (buoyancy + windDir) * gDeltaTime;
            p.Velocity += buoyancy * cbMainPass.DeltaTime;

            float3 field = curlNoise(p.Pos * 0.25 + cbMainPass.TotalTime * 0.5);
            p.Velocity += field * 0.2 * cbMainPass.DeltaTime;
            
            p.Velocity *= 0.96;
            p.Pos += p.Velocity * cbMainPass.DeltaTime;
            
            p.Size += cbParticle.ParticleSize * 0.4 * cbMainPass.DeltaTime;
            
            float t_fade = p.LifeTime / 5.0f;
            p.Color.a = saturate(1.0 - (1.0 - t_fade) * (1.0 - t_fade)) * 0.8f;
            
            p.Color.rgb = lerp(float3(0.4, 0.4, 0.4), float3(0.15, 0.15, 0.15), saturate(t_fade));
            
            //float ratio = saturate(p.LifeTime / p.StartLifeTime);
            //p.Color.a = ratio * 0.8f;
            //p.Color.rgb = lerp(
            //    float3(0.15, 0.15, 0.15),
            //    float3(0.4, 0.4, 0.4),
            //    ratio
            //);

            ParticlePool[idx] = p;
            AliveListAppend.Append(idx);
        }
        else
        {
            DeadListsAppend[1 - cbParticle.CurrentDeadList].Append(idx);
        }
    }
    else
    {
        DeadListsAppend[1 - cbParticle.CurrentDeadList].Append(idx);
    }
}