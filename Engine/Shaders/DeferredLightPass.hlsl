#include "CBufferStructures.hlsl"

Texture2D   DiffuseMap     : register(t0);
Texture2D   DepthMaps    : register(t1);
Texture2D   NormalMap      : register(t2);
Texture2D   MaterialFresnelRoughnessMap : register(t3);

Texture2DArray ShadowMaps : register(t4);

TextureCube IrradianceMap   : register(t5);
TextureCube PrefilterEnvMap : register(t6);
Texture2D BRDF_LUT          : register(t7);

SamplerComparisonState ShadowSampler : register(s0);
SamplerState samLinearClamp          : register(s1);

ConstantBuffer<MainPassCB> cbMainPass : register(b0);

ConstantBuffer<LightCB> cbLight : register(b1);

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

VertexOut VS_Bounded(VertexIn vin)
{
    VertexOut vout;
    
    vout.PosH = mul(float4(vin.PosL, 1.0f), mul(cbLight.World, cbMainPass.ViewProj));
    
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

float CalcRTShadow(Light light, uint2 TexelCoord)
{
    static const float Kernel[11] =
    {
        0.000003, 0.000229, 0.005977, 0.060598, 0.24173,
        0.382925, 0.24173, 0.060598, 0.005977, 0.000229, 0.000003
    };
    
    uint width, height, numLayers, numMips;
    ShadowMaps.GetDimensions(0, width, height, numLayers, numMips);
    
    float2 viewportSize = float2(width, height);
    float blurStrength = 10.0f;
    
    float result = 0.0f;
    
    [unroll]
    for (int x = -5; x <= 5; x++)
    {
        [unroll]
        for (int y = -5; y <= 5; y++)
        {
            float2 offset = float2(x, y) / viewportSize * blurStrength;
            uint2 sampleCoord = TexelCoord + uint2(offset * viewportSize);
            
            if (sampleCoord.x < width && sampleCoord.y < height)
            {
                float kernelValue = Kernel[x + 5] * Kernel[y + 5];
                float sampleValue = ShadowMaps.Load(int4(sampleCoord, 0, 0)).x;
                result += sampleValue * kernelValue;
            }
        }
    }
    
    return result;
}

float CalcShadowFactor(float3 WorldPosition, float3 Normal, uint ShadowMapIndex)
{
    float4 shadowPosH = mul(float4(WorldPosition, 1.f), cbLight.ShadowTransform[ShadowMapIndex]);
    
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;
    
    // Depth in NDC space.
    float depth = shadowPosH.z;

    uint width, height, numLayers, numMips;
    ShadowMaps.GetDimensions(0, width, height, numLayers, numMips);

    // Slope-Scaled Depth Bias
    float3 lightDir = normalize(-cbLight.lightData.Direction);
    float slopeBias = 0.005 * tan(acos(saturate(dot(Normal, lightDir))));
    slopeBias = clamp(slopeBias, 0.001, 0.05);
    float biasedDepth = depth - (0.001 + slopeBias);
    
    //PCF
    float dx = 1.f / (float) width;

    float percentLit = 0.0f;
    float totalWeight = 0.0f;
    const float2 offsets[9] =
    {
        float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx, +dx), float2(0.0f, +dx), float2(dx, +dx)
    };

    // Weights for smoother filtering
    const float weights[9] =
    {
        0.0625, 0.125, 0.0625,
        0.125, 0.25, 0.125,
        0.0625, 0.125, 0.0625
    };
    
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float2 sampleCoord = shadowPosH.xy + offsets[i];
        
        // Skip samples outside the shadow map
        if (sampleCoord.x >= 0.f && sampleCoord.x <= 1.f &&
            sampleCoord.y >= 0.f && sampleCoord.y <= 1.f)
        {
            percentLit += ShadowMaps.SampleCmpLevelZero(ShadowSampler,
                float3(sampleCoord, ShadowMapIndex), biasedDepth).r * weights[i];
            totalWeight += weights[i];
        }
    }
    
    // Normalize by actual weight sum (in case some samples were skipped)
    return totalWeight > 0 ? percentLit / totalWeight : 1.0f;
}

#define PI 3.14159265359

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float4 PS(VertexOut pin) : SV_Target
{
    float2 UV = pin.PosH.xy / cbMainPass.RenderTargetSize;
    uint2 TexelCoord = pin.PosH.xy;
    //loading GBuffer channels
    float4 MatParams = MaterialFresnelRoughnessMap.Load(int3(TexelCoord, 0));
    float ScreenDepth = DepthMaps.Load(int3(TexelCoord, 0)).w;
    float4 NormalChannel = NormalMap.Load(int3(TexelCoord, 0));
    float4 Diffuse = DiffuseMap.Load(int3(TexelCoord, 0));
    float Metallic = NormalChannel.w;

    float3 WorldPosition = ReconstructWorldPosition(UV, ScreenDepth);
    float3 MatFresnelR0 = MatParams.xyz;
    float MatRoughness = MatParams.w;
    float3 Normal = NormalChannel.rgb;
    
    // Vector from point being lit to eye.
    float3 toEyeW = cbMainPass.CameraPos - WorldPosition;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize
    float NdotV = max(dot(Normal, toEyeW), 0.0);

    const float shininess = 1.0f - MatRoughness;
    Material mat = { Diffuse, MatFresnelR0, shininess };
    
    //discard if there is no geometry in Gbuffer at current pixel
    if (length(Normal) < 0.01f)
        discard;
    
    float3 Lighting;
    
    //calculate light based on its type
    if (cbLight.lightData.LightType == 0)
    {
        float shadowFactor = 1.f;
        float distanceFromEye = length(WorldPosition - cbMainPass.CameraPos);
        
        //for (uint cascade = 0; cascade < 5; cascade++)
        //{
        //    float factor = CalcShadowFactor(WorldPosition, Normal, cascade);
        //    if (factor < 0.3f)
        //    {
        //        shadowFactor = factor;
        //        break;
        //    }
        //}
        
        shadowFactor = CalcRTShadow(cbLight.lightData, TexelCoord);

        float3 lightDir = normalize(-cbLight.lightData.Direction);
        float3 halfVec = normalize(toEyeW + lightDir);
        float NdotL = max(dot(Normal, lightDir), 0.0);
        
        float3 F0 = lerp(0.04.xxx, Diffuse.rgb, Metallic);
        
        bool gUsePBR = true;
        if (gUsePBR == 0)
            {
                // Lambert
                float3 diffuse = Diffuse.rgb * NdotL;

                float specPower = lerp(4.0, 128.0, 1.0 - MatRoughness);
                float NdotH = max(dot(Normal, halfVec), 0.0);
                float specTerm = pow(NdotH, specPower);
                float3 specular = MatFresnelR0 * specTerm;

            float3 radiance = cbLight.lightData.Strength * cbLight.lightData.Color;

                float3 Lo = (diffuse + specular) * radiance;

                Lighting = shadowFactor * Lo;
            }
        else
            {
                float3 F = FresnelSchlick(max(dot(halfVec, toEyeW), 0.0), F0);
                float NDF = DistributionGGX(Normal, halfVec, MatRoughness);
                float G = GeometrySmith(Normal, toEyeW, lightDir, MatRoughness);
                float3 specular = (NDF * G * F) / (4.0 * NdotV * NdotL + 0.001);

                float3 kS = F;
                float3 kD = (1.0 - kS) * (1.0 - Metallic);

            float3 radiance = cbLight.lightData.Strength * cbLight.lightData.Color;
                float3 Lo = (kD * Diffuse.rgb / PI + specular) * radiance * NdotL;

                Lighting = shadowFactor * Lo;
            }
    }
    else if (cbLight.lightData.LightType == 1)
    {
        if (length(cbLight.lightData.Position - WorldPosition) > (cbLight.lightData.Strength.x * 10))
            discard;
        
        float3 lightToPixel = WorldPosition - cbLight.lightData.Position;
        float distToLight = length(lightToPixel);
        lightToPixel /= distToLight;
    
        // determine shadow map cube face index
        float3 absDir = abs(lightToPixel);
        uint faceIndex = 0;
        if (absDir.x >= absDir.y && absDir.x >= absDir.z)
            faceIndex = (lightToPixel.x > 0) ? 0 : 1;
        else if (absDir.y >= absDir.z)
            faceIndex = (lightToPixel.y > 0) ? 2 : 3;
        else
            faceIndex = (lightToPixel.z > 0) ? 4 : 5;
        
        float3 L = normalize(cbLight.lightData.Position - WorldPosition);
        float NdotL = max(dot(Normal, L), 0.0);
        float3 H = normalize(L + toEyeW);
        
        bool gUsePBR = true;
        if (gUsePBR == 0)
        {
            float3 radiance = cbLight.lightData.Strength * cbLight.lightData.Color;
            float specPower = lerp(4.0, 128.0, 1.0 - MatRoughness);
            float specTerm = pow(max(dot(Normal, H), 0.0), specPower);

            float3 diffuse = Diffuse.rgb * NdotL;
            float3 specular = MatFresnelR0 * specTerm;


            Lighting = CalcRTShadow(cbLight.lightData, TexelCoord) * (diffuse + specular) * radiance;
        }
        else
        {
            Lighting = CalcRTShadow(cbLight.lightData, TexelCoord)
                 * ComputePointLight(cbLight.lightData, mat, WorldPosition, Normal, toEyeW) * cbLight.lightData.Color;
           

        }
    }
    else if (cbLight.lightData.LightType == 2)
    {
        // no cascades or complex maps here. using shadow map 0
        Lighting = CalcRTShadow(cbLight.lightData, TexelCoord) * ComputeSpotLight(cbLight.lightData, mat, WorldPosition, Normal, toEyeW) * cbLight.lightData.Color;
    }

    float4 litColor = float4(Lighting, 0.f);

    litColor.a = Diffuse.a;

    return litColor;    
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float
roughness)
{
    return F0 + (max(1.0 - roughness, F0) - F0) * pow(saturate(1.0 -
cosTheta), 5.0);
}

float4 PS_AddAmbient(VertexOut pin) : SV_Target
{
    uint2 TexelCoord = pin.PosH.xy;
    
    float4 MatParams = MaterialFresnelRoughnessMap.Load(int3(TexelCoord, 0));
    float ScreenDepth = DepthMaps.Load(int3(TexelCoord, 0)).w;
    float4 NormalChannel = NormalMap.Load(int3(TexelCoord, 0));
    float4 Diffuse = DiffuseMap.Load(int3(TexelCoord, 0));
    
    float3 albedo = Diffuse.rgb;
    float roughness = MatParams.w;
    float metallic = NormalChannel.w;
    float3 normal = normalize(NormalChannel.rgb);
    
    if(length(normal) < 0.01f)
        discard;
    
    bool gUsePBR = true;
    if (gUsePBR == 0)
    {
        float3 ambient = cbMainPass.AmbientLight.rgb * Diffuse.rgb;
        return float4(ambient, Diffuse.a);
    }
    
    float2 UV = pin.PosH.xy / cbMainPass.RenderTargetSize;
    float3 worldPos = ReconstructWorldPosition(UV, ScreenDepth);
    float3 viewDir = normalize(cbMainPass.CameraPos - worldPos);
    float NdotV = max(dot(normal, viewDir), 0.0);
    
    // --- PBR IBL ---
    float3 F0 = lerp(0.04.xxx, albedo, metallic);
    float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    
    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= (1.0 - metallic);
    
    float3 irradiance = IrradianceMap.Sample(samLinearClamp, normal).rgb;
    float3 diffuse = irradiance * albedo;
    
    uint width, height, NumMips;
    PrefilterEnvMap.GetDimensions(0, width, height, NumMips);
    
    float3 R = reflect(-viewDir, normal);
    float3 prefilteredColor = PrefilterEnvMap.SampleLevel(samLinearClamp, R, roughness * NumMips).rgb;
    float2 brdf = BRDF_LUT.Sample(samLinearClamp, float2(NdotV, roughness)).rg;
    float3 specular = prefilteredColor * (F * brdf.x + brdf.y);
    
    float ao = 1.0f;
    float3 ambient = (kD * diffuse + specular) * ao * 0.1f.xxx;
    
    return float4(ambient, Diffuse.a);
}