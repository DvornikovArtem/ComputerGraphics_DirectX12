// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

Texture2D   gDiffuseMap     : register(t0);
Texture2D   gEmissiveMap    : register(t1);
Texture2D   gNormalMap      : register(t2);
Texture2D   gMaterialAlbedoMap : register(t3);
Texture2D   gMaterialFresnelRoughnessMap : register(t4);

Texture2DArray gShadowMaps : register(t5);

SamplerComparisonState gShadowSampler : register(s0);

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

// Constant data that varies per light.
cbuffer cbPerLight : register(b1)
{
    Light CurrentLight;
    float4x4 gWorld;
    float4x4 View[6];
    float4x4 Proj[6];
    float4x4 ShadowTransform[6];
    float4 CascadeDistances;
}


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
    
    vout.PosH = mul(float4(vin.PosL, 1.0f), mul(gWorld, gViewProj));
    
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

float CalcShadowFactor(float3 WorldPosition, float3 Normal, uint ShadowMapIndex)
{
    float4 shadowPosH = mul(float4(WorldPosition, 1.f), ShadowTransform[ShadowMapIndex]);
    
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;
    
    // Depth in NDC space.
    float depth = shadowPosH.z;

    uint width, height, numLayers, numMips;
    gShadowMaps.GetDimensions(0, width, height, numLayers, numMips);

    // Slope-Scaled Depth Bias
    float3 lightDir = normalize(-CurrentLight.Direction);
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
            percentLit += gShadowMaps.SampleCmpLevelZero(gShadowSampler,
                float3(sampleCoord, ShadowMapIndex), biasedDepth).r * weights[i];
            totalWeight += weights[i];
        }
    }
    
    // Normalize by actual weight sum (in case some samples were skipped)
    return totalWeight > 0 ? percentLit / totalWeight : 1.0f;
}

float4 PS(VertexOut pin) : SV_Target
{
    float2 UV = pin.PosH.xy / gRenderTargetSize;
    uint2 TexelCoord = pin.PosH.xy;
    //loading GBuffer channels
    float4 MatAlbedo = gMaterialAlbedoMap.Load(int3(TexelCoord, 0));
    float4 MatParams = gMaterialFresnelRoughnessMap.Load(int3(TexelCoord, 0));
    float4 Emissive = gEmissiveMap.Load(int3(TexelCoord, 0));
    float4 NormalChannel = gNormalMap.Load(int3(TexelCoord, 0));
    float4 Diffuse = gDiffuseMap.Load(int3(TexelCoord, 0)) * MatAlbedo;
    
    float3 WorldPosition = ReconstructWorldPosition(UV, Emissive.w);
    float3 MatFresnelR0 = MatParams.xyz;
    float MatRoughness = MatParams.w;
    float3 Normal = NormalChannel.rgb;
    
    // Vector from point being lit to eye.
    float3 toEyeW = gEyePosW - WorldPosition;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize

    const float shininess = 1.0f - MatRoughness;
    Material mat = { Diffuse, MatFresnelR0, shininess };
    
    //discard if there is no geometry in Gbuffer at current pixel
    if (length(Normal) < 0.01f)
        discard;
    
    float3 Lighting;
    
    //calculate light based on its type
    if (CurrentLight.LightType == 0)
    {
        //using cascaded shadow maps
        
        // finding first cascade with that casts a shadow
        float shadowFactor = 1.f;
        float distanceFromEye = length(WorldPosition - gEyePosW);
    
        for (uint cascade = 0; cascade < 4; cascade++)
        {
            float factor = CalcShadowFactor(WorldPosition, Normal, cascade);
            if(factor < 0.3f)
            {
                shadowFactor = factor;
                break;
            }     
        }
    
        Lighting = shadowFactor * ComputeDirectionalLight(CurrentLight, mat, Normal, toEyeW) * CurrentLight.Color;
    }
    else if (CurrentLight.LightType == 1)
    {
        if (length(CurrentLight.Position - WorldPosition) > (CurrentLight.Strength.x * 10))
            discard;
        
        float3 lightToPixel = WorldPosition - CurrentLight.Position;
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
        
        Lighting = CalcShadowFactor(WorldPosition, Normal, faceIndex) * ComputePointLight(CurrentLight, mat, WorldPosition, Normal, toEyeW) * CurrentLight.Color;
    }
    else if(CurrentLight.LightType == 2)
    {
        // no cascades or complex maps here. using shadow map 0
        Lighting = CalcShadowFactor(WorldPosition, Normal, 0) * ComputeSpotLight(CurrentLight, mat, WorldPosition, Normal, toEyeW) * CurrentLight.Color;
    }

    float4 litColor = float4(Lighting, 0.f);

    litColor.a = Diffuse.a;

    return litColor;    
}

float4 PS_AddAmbient(VertexOut pin) : SV_Target
{
    //same as PS but only adds ambient light
    float4 MatAlbedo = gMaterialAlbedoMap.Load(int3(pin.PosH.xy, 0));
    float4 DiffuseAlbedo = gDiffuseMap.Load(int3(pin.PosH.xy, 0)) * MatAlbedo;

    float4 ambient = gAmbientLight * DiffuseAlbedo;

    float4 litColor = ambient;

    litColor.a = DiffuseAlbedo.a;

    return litColor;
}