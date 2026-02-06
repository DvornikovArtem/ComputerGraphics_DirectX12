#include "CBufferStructures.hlsl"

Texture2D   DiffuseMap     : register(t0);
Texture2D   DepthMaps    : register(t1);
Texture2D   NormalMap      : register(t2);
Texture2D   ObjectOutlines : register(t3);

SamplerState samPointWrap : register(s0);
SamplerState samPointClamp : register(s1);
SamplerState samLinearWrap : register(s2);
SamplerState samLinearClamp : register(s3);
SamplerState samAnisotropicWrap : register(s4);
SamplerState samAnisotropicClamp : register(s5);

ConstantBuffer<MainPassCB> cbMainPass : register(b0);

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
    float4 viewPos = mul(clipPos, cbMainPass.InvViewProj);
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
    
    float2 uvRed = clamp(UV - dir * distortion * 1.0, 0.0, 1.0);
    float2 uvGreen = clamp(UV - dir * distortion * 0.5, 0.0, 1.0);
    float2 uvBlue = clamp(UV + dir * distortion * 1.0, 0.0, 1.0);
    
    uint2 texCoordRed = uint2(uvRed * cbMainPass.ViewportSize);
    uint2 texCoordGreen = uint2(uvGreen * cbMainPass.ViewportSize);
    uint2 texCoordBlue = uint2(uvBlue * cbMainPass.ViewportSize);
    
    int3 coordRed = int3(clamp(texCoordRed, 0, cbMainPass.ViewportSize - 1), 0);
    int3 coordGreen = int3(clamp(texCoordGreen, 0, cbMainPass.ViewportSize - 1), 0);
    int3 coordBlue = int3(clamp(texCoordBlue, 0, cbMainPass.ViewportSize - 1), 0);
    
    float red = DiffuseMap.Load(coordRed).r;
    float green = DiffuseMap.Load(coordGreen).g;
    float blue = DiffuseMap.Load(coordBlue).b;
   
    
    return float4(red, green, blue, 1.0);
}

float4 DepthOfField(float depth, float2 TexelCoord, float4 Color)
{
    float gFocalDistance = 0.2f;
    float gFocalRange = 0.1f;
    float gBlurRadius = 8.f;
    float gDOFIntensity = 1.f;
    float gNearCutoff = 0.9f;


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
        float2 texelSize = 1.0 / cbMainPass.ViewportSize;
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
                int3 sampleCoord = int3(clamp(TexelCoord + int2(x, y), int2(0, 0), int2(cbMainPass.ViewportSize) - int2(1, 1)), 0);
                blurredColor += DiffuseMap.Load(sampleCoord) * weight;
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
    
    float4 clipPos = mul(float4(gSunPosW, 1.0), cbMainPass.ViewProj);
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
        
        
        float ScreenDepth = DepthMaps.SampleLevel(samLinearClamp, sampleUV, 0).w;
       
        if (ScreenDepth < depth)
            break;
        
        float4 sampleColor = DiffuseMap.SampleLevel(samLinearClamp, sampleUV, 0);
        color += sampleColor * illuminationDecay * gGodRaysWeight;
        illuminationDecay *= gGodRaysDecay;
    }
    
    color *= gGodRaysIntensity;
    return saturate(color);
}

float4 DrawOutlines(uint2 TexelCoord, float2 UV)
{
    float4 depthMaps = DepthMaps.Load(int3(TexelCoord, 0));
    //if outlined depth stencil != overall depth stencil
    if (depthMaps.x != depthMaps.w)
    {
        //blur outline texture and return it
        static const float Kernel[11] =
        { 0.000003, 0.000229, 0.005977, 0.060598, 0.24173, 0.382925, 0.24173, 0.060598, 0.005977, 0.000229, 0.000003 };
    
        float4 result = 0;
        float blurStrength = 5;
  
        [unroll]
        for (int x = -5; x <= 5; x++)
        {
            [unroll]
            for (int y = -5; y <= 5; y++)
            {
                float2 offset = float2(x, y) / cbMainPass.ViewportSize * blurStrength;
                float kernelValue = Kernel[x + 5] * Kernel[y + 5];
                result += ObjectOutlines.Sample(samLinearClamp, UV + offset) * kernelValue;
            }
        }
        return result;
    }
    return 0.f.xxxx;
}

float4 PS(VertexOut pin) : SV_Target
{
    uint2 TexelCoord = pin.PosH.xy;
    float2 UV = TexelCoord / cbMainPass.ViewportSize;
    //loading GBuffer channels
    float ScreenDepth = DepthMaps.Load(int3(TexelCoord, 0)).w;
    float4 NormalChannel = NormalMap.Load(int3(TexelCoord, 0));
    float4 Color = DiffuseMap.Load(int3(TexelCoord, 0));
    
    
    float3 WorldPosition = ReconstructWorldPosition(UV, ScreenDepth);
    float3 Normal = NormalChannel.rgb;

    //Do your cool post-processing here
    
    
    //gamma 2.2 correction
    Color.xyz = pow(saturate(Color.xyz), 1.0 / 2.2);
    return Color;
}