Texture2D   gDiffuseMap     : register(t0);
Texture2D   gEmissiveMap    : register(t1);
Texture2D   gNormalMap      : register(t2);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per frame.
cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

	float4 gFogColor;
	float gFogStart;
	float gFogRange;
	float2 cbPerObjectPad2;
    
    float4 Decals[3];
};

struct VertexIn
{
	float3 PosL    : POSITION;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
};


VertexOut VS_FSQuad(uint vertexID : SV_VertexID)
{
    //full-screen quad
    
    float2 verts[3] =
    {
        float2(-1, -1),
        float2(-1, 3),
        float2(3, -1)
    };
    
    
    VertexOut vout;
    vout.PosH = float4(verts[vertexID], 0, 1);
    return vout;
}

float3 ReconstructWorldPosition(float2 UV, float depth)
{
    //magic DirectX texcoord mutations
    float4 clipPos;
    clipPos.x = UV.x * 2.0f - 1.0f;
    clipPos.y = 1.0f - UV.y * 2.0f;
    clipPos.z = depth;
    clipPos.w = 1.0f;

    //transform into world space
    float4 viewPos = mul(clipPos, gInvViewProj);
    viewPos.xyz /= viewPos.w;

    return viewPos.xyz;
}

float4 ChromaticAbberation(float2 UV)
{
    float2 gDistortionCenter = float2(0.5, 0.5);
    float gAberrationStrength = 0.025f;
    float gRadialScale = 2.f;
    
    float2 dir = UV - gDistortionCenter;
    float distanceFromCenter = length(dir);
    dir = normalize(dir);
    
    float distortion = gAberrationStrength * distanceFromCenter * gRadialScale;
    
    float edgeFade = 1.0 - smoothstep(0.7, 1.0, distanceFromCenter);
    distortion *= edgeFade;
    
    // Смещение с ограничением координат
    float2 uvRed = clamp(UV - dir * distortion * 1.0, 0.0, 1.0);
    float2 uvGreen = clamp(UV - dir * distortion * 0.5, 0.0, 1.0);
    float2 uvBlue = clamp(UV + dir * distortion * 1.0, 0.0, 1.0);
    
    // Преобразование в текстурные координаты с проверкой границ
    uint2 texCoordRed = uint2(uvRed * gRenderTargetSize);
    uint2 texCoordGreen = uint2(uvGreen * gRenderTargetSize);
    uint2 texCoordBlue = uint2(uvBlue * gRenderTargetSize);
    
    // Проверка на выход за пределы текстуры
    int3 coordRed = int3(clamp(texCoordRed, 0, gRenderTargetSize - 1), 0);
    int3 coordGreen = int3(clamp(texCoordGreen, 0, gRenderTargetSize - 1), 0);
    int3 coordBlue = int3(clamp(texCoordBlue, 0, gRenderTargetSize - 1), 0);
    
    // Загрузка данных с защитой от выхода за границы
    float red = gDiffuseMap.Load(coordRed).r;
    float green = gDiffuseMap.Load(coordGreen).g;
    float blue = gDiffuseMap.Load(coordBlue).b;
   
    
    return float4(red, green, blue, 1.0);
}

float4 DepthOfField(float depth, float2 TexelCoord, float4 Color)
{
    float gFocalDistance = 0.2f; // Фокус на ближних объектах (0.0-0.3)
    float gFocalRange = 0.1f; // Узкая зона резкости для четкого разделения
    float gBlurRadius = 8.f; // Сильное размытие для дальних объектов
    float gDOFIntensity = 1.f; // Полная интенсивность эффекта
    float gNearCutoff = 0.9f; // Граница, до которой объекты остаются резкими


    float blurAmount = 0.0f;

    if (depth > gNearCutoff)
    {
        float adjustedDepth = (depth - gNearCutoff) / (1.0 - gNearCutoff);
        float depthDifference = abs(adjustedDepth - gFocalDistance);
    
        blurAmount = smoothstep(0.0, gFocalRange, depthDifference);
        blurAmount = pow(blurAmount, 3.0) * gDOFIntensity;
    }

    if (blurAmount > 0.001f)
    {
        float2 texelSize = 1.0 / gRenderTargetSize;
        float radius = blurAmount * gBlurRadius;
    
        float4 blurredColor = float4(0, 0, 0, 0);
        float weightSum = 0.0;
    
        // 7x7 blurring
        const int kernelSize = 3;
        for (int y = -kernelSize; y <= kernelSize; y++)
        {
            for (int x = -kernelSize; x <= kernelSize; x++)
            {
                float2 offset = float2(x, y) * texelSize * radius;
                float distanceSq = dot(offset, offset);
                float weight = exp(-distanceSq / (2.0 * radius * radius));
            
                // Sample with offset
                int3 sampleCoord = int3(clamp(TexelCoord + int2(x, y), int2(0, 0), int2(gRenderTargetSize) - int2(1, 1)), 0);
                blurredColor += gDiffuseMap.Load(sampleCoord) * weight;
                weightSum += weight;
            }
        }
    
        blurredColor /= weightSum;
    
        // Blend with original and blurred image
        Color = lerp(Color, blurredColor, blurAmount);
    }
    
    return Color;
}

float4 GodRays(float2 UV, float3 worldPos, float depth)
{
    float3 gSunPosW = float3(577.35, -577.35, 577.35);
    float gGodRaysIntensity = 0.1f;
    float gGodRaysDensity = 0.2f;
    float gGodRaysWeight = 0.25f;
    float gGodRaysDecay = 0.95f;
    int gGodRaysSamples = 25;
    
    float4 clipPos = mul(float4(gSunPosW, 1.0), gViewProj);
    clipPos.xyz /= clipPos.w * (clipPos.w > 0 ? -1 : 1);
    float2 sunUV = 0.5 * clipPos.xy + float2(0.5, 0.5);
    sunUV.y = 1.0 - sunUV.y;
   
    float2 dir = normalize(sunUV - UV);
    float2 deltaUV = dir * gGodRaysDensity / gGodRaysSamples;
    float4 color = float4(0, 0, 0, 0);
    float illuminationDecay = 1.0;
    
    [loop]
    for (int i = 0; i < gGodRaysSamples; i++)
    {
        UV += deltaUV;
        float2 sampleUV = clamp(UV, 0.0, 1.0);
        
        
        float4 emissive = gEmissiveMap.SampleLevel(gsamLinearClamp, sampleUV, 0);
        float sampleDepth = emissive.w;
       
        if (sampleDepth < depth)
            break;
        
        float4 sampleColor = gDiffuseMap.SampleLevel(gsamLinearClamp, sampleUV, 0);
        color += sampleColor * illuminationDecay * gGodRaysWeight;
        illuminationDecay *= gGodRaysDecay;
    }
    
    color *= gGodRaysIntensity;
    return saturate(color);
}

float4 PS(VertexOut pin) : SV_Target
{
    uint2 TexelCoord = pin.PosH.xy;
    float2 UV = TexelCoord / gRenderTargetSize;
    //loading GBuffer channels
    float4 Emissive = gEmissiveMap.Load(int3(TexelCoord, 0));
    float4 NormalChannel = gNormalMap.Load(int3(TexelCoord, 0));
    float4 Color = gDiffuseMap.Load(int3(TexelCoord, 0));
    
    
    float3 WorldPosition = ReconstructWorldPosition(UV, Emissive.w);
    float3 Normal = NormalChannel.rgb;

    //Do your cool post-processing here
   
    //Color = ChromaticAbberation(UV);
    //Color = DepthOfField(Emissive.w, TexelCoord, Color);

    //Color += GodRays(UV, WorldPosition, Emissive.w);
    
    //gamma 2.2 correction
    Color.xyz = pow(saturate(Color.xyz), 1.0 / 2.2);
    return Color;
}
