#include "CBufferStructures.hlsl"

Texture2D DiffuseMap : register(t0);
Texture2D DepthMaps : register(t1);
Texture2D NormalMap : register(t2);
Texture2D ObjectOutlines : register(t3);

SamplerState samPointWrap : register(s0);
SamplerState samPointClamp : register(s1);
SamplerState samLinearWrap : register(s2);
SamplerState samLinearClamp : register(s3);
SamplerState samAnisotropicWrap : register(s4);
SamplerState samAnisotropicClamp : register(s5);

ConstantBuffer<MainPassCB> cbMainPass : register(b0);
ConstantBuffer<AtmosphereCB> cbAtm : register(b1);


static const float PI = 3.14159265f;


struct VertexIn
{
    float3 PosL : POSITION;
};


struct VertexOut
{
    float4 PosH : SV_POSITION;
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


// World position reconstruction
float3 ReconstructWorldPosition(float2 UV, float depth)
{
    // magic DirectX texcoord mutations
    float4 clipPos;
    clipPos.x = UV.x * 2.0f - 1.0f;
    clipPos.y = 1.0f - UV.y * 2.0f;
    clipPos.z = depth;
    clipPos.w = 1.0f;

    // transform into world space
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



float RayleighPhase(float mu)
{
    return 3.0f / (16.0f * PI) * (1.0f + mu * mu);
}

float MiePhaseHG(float mu, float g)
{
    return (1.0f - g * g) / (4.0f * PI * pow(1.0f + g * g - 2.0f * g * mu, 1.5f));
}


struct AerialResult
{
    float3 inscatter;
    float3 transmittance;
};

AerialResult IntegrateAerial(float3 camPos, float tMax, float3 V)
{
    tMax = max(tMax, 0.5f);
    
    int N = clamp((int) ceil(tMax / 2500.0f), 16, 64);
    float dt = tMax / N;

    float3 pos = camPos + V * (0.5f * dt);
    float3 Absorption = float3(1, 1, 1);
    float3 L = float3(0, 0, 0);

    float mu = dot(cbAtm.SunDirection, V);
    float phaseR = RayleighPhase(mu);
    float phaseM = MiePhaseHG(mu, cbAtm.MieG);

    [loop]
    for (int i = 0; i < N; i++)
    {
        float h = max(0.0f, pos.y - cbAtm.GroundLevelY);
        float dR = exp(-h / max(1e-3, cbAtm.RayleighScaleHeight)) * cbAtm.DensityScale;
        float dM = exp(-h / max(1e-3, cbAtm.MieScaleHeight)) * cbAtm.DensityScale;

        //float3 sigma_s = cbAtm.BetaRayleigh * dR + cbAtm.BetaMieSca * dM;
        //float3 sigma_t = cbAtm.BetaRayleigh * dR + cbAtm.BetaMieExt * dM;
        float3 sigma_s = cbAtm.BetaRayleigh * dR + cbAtm.BetaMieSca * dM;
        float3 sigma_t = cbAtm.BetaRayleigh * dR + cbAtm.BetaMieExt * dM;

        // Absorption for sun light (slide 5) - if expand the formula from the slide using a Taylor series, it becomes equivalent to the formula below (to the exponential) for small dt
        // This is also the Lambert–Beer law (the length of the sun ray is set to 50000)
        float3 T_light = exp(-sigma_t * 50000.0f);

        // In-Scattering (slide 7 in the formula to the right of the '+')
        float3 S = (phaseR * cbAtm.BetaRayleigh * dR + phaseM * cbAtm.BetaMieSca * dM) * cbAtm.SunIntensity * T_light;

        // 
        L += Absorption * S * dt;
        
        // Absorption (slide 5) - if expand the formula from the slide using a Taylor series, it becomes equivalent to the formula below (to the exponential) for small dt
        // This is also the Lambert–Beer law
        Absorption *= exp(-sigma_t * dt);

        pos += V * dt;
    }

    AerialResult r;
    r.inscatter = L;
    r.transmittance = Absorption;
    return r;
}


float4 PS(VertexOut pin) : SV_Target
{
    // integer texel coords + half-texel UV
    uint2 TexelCoord = (uint2) pin.PosH.xy;
    float2 UV = (float2(TexelCoord) + 0.5f) / cbMainPass.ViewportSize;
    

    //loading GBuffer channels
    float depthDevice = DepthMaps.Load(int3(TexelCoord, 0)).w;
    float4 NormalChannel = NormalMap.Load(int3(TexelCoord, 0));
    float4 Color = DiffuseMap.Load(int3(TexelCoord, 0));

    
    float3 WorldPosition = ReconstructWorldPosition(UV, depthDevice);
    
    
    // Do your cool post-processing here
    
    
    Color += DrawOutlines(TexelCoord, UV);

    float4 effects = 0;
    effects = ChromaticAbberation(UV);
    effects = DepthOfField(depthDevice, TexelCoord, effects);
    effects += GodRays(UV, WorldPosition, depthDevice);
    // effects += bloom(...);
    // effects += lensDirt(...);
    effects *= cbMainPass.postEffectsExposure;
    Color += effects;
    
    
    
    // Atmosphere
    float3 Normal = NormalChannel.rgb;
    float normalLen2 = dot(Normal, Normal);
    
    // If the depth in the texel is close to 1.0f, it is sky. But if we will have custom scheme in GBuffer depth channel, this check could not work.
    // So we can check the square of normal. If it is close to 0.0f, so we did not touch this texel and 99.9% probability it is a sky
    bool isSky = (depthDevice >= 1.0f - 1e-6f) || (normalLen2 < 1e-6f);

    
    float3 camPos = cbMainPass.CameraPos;
    
    // Compute the point in the world that lies on the infinitely distant background in the direction of the camera through this pixel (if we will write 1.0f - we could have some compute problems because of NaNs)
    float3 P_far = ReconstructWorldPosition(UV, 0.9999f);
    // Normalized view vector for a given pixel
    float3 V = normalize(P_far - camPos);
    
    
    // The maximum distance for integrating atmospheric scattering along the ray
    float tMaxSky;
    
    // If V.y > 0 -> the ray is directed upward (into the sky). Then the intersection point of the ray with the upper boundary of the atmosphere is computed as follows
    if (V.y > 1e-6f) tMaxSky = (cbAtm.AtmosphereTopY - camPos.y) / V.y;
    // If V.y < 0, the ray is looking downward, toward the ground. Look for the intersection point with the lower boundary of the atmosphere
    else if (V.y < -1e-6f)
    {
        float tGround = (cbAtm.GroundLevelY - camPos.y) / V.y;
        tMaxSky = max(0.0f, tGround);
        
        // To make the atmosphere 'visible' even below the horizon, we manually set 50000 here
        tMaxSky = 50000.0f;
    }
    // If V.y ~ 0, the ray travels almost horizontally; it either does not intersect the atmosphere or will intersect it very far away. Therefore, a very large distance is assigned.
    else tMaxSky = 50000.0f;
    
    
    
    // Ray marching is invoked: start = camPos, we move along the ray V up to the distance tMaxSky = 50000.
    // The function integrates in steps: light absorption, in-scattering, Rayleigh and Mie scattering. This produces the sky color and haze.
    AerialResult arSky = IntegrateAerial(camPos, tMaxSky, V);

    
    
    // If the pixel is sky
    if (isSky)
    {
        // - transmittance – the current color (which may include post-effects) is attenuated by the atmosphere (the farther we look, the denser the air -> the darker it becomes through the atmosphere)
        // - inscatter – air molecules scatter light -> a bluish tint, and the direct sunlight is scattered along its path toward you
        Color.rgb = Color.rgb * arSky.transmittance + arSky.inscatter;
    }
    // Otherwise — the pixel belongs to a scene object
    else
    {
        // For objects we take tMaxGeo = the distance from the camera to the object. So that the atmosphere is integrated only up to the object, not 50 km ahead
        float tMaxGeo = length(WorldPosition - camPos);
        
        // Ray marching
        AerialResult arGeo = IntegrateAerial(camPos, tMaxGeo, V);
        
        float3 objColor = Color.rgb * arGeo.transmittance;
        
        float fogStartDepth = 0.1f;
        float fogEndDepth = 0.95f; 
        float fogFactor = saturate((depthDevice - fogStartDepth) / max(1e-3f, fogEndDepth - fogStartDepth));
        
        float fogIntensity = 1.0f;

        float3 fogAdd = arGeo.inscatter * fogIntensity;
        
        Color.rgb = objColor + fogAdd * fogFactor;
    }
    // End Of The Atmosphere
    
    
    
    Color.rgb = pow(saturate(Color.rgb), 1.0 / 2.2);
    return Color;
}
