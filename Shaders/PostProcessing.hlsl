Texture2D   gDiffuseMap     : register(t0);
Texture2D   gEmissiveMap    : register(t1);
Texture2D   gNormalMap      : register(t2);

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
    
    // —мещение с ограничением координат
    float2 uvRed = clamp(UV - dir * distortion * 1.0, 0.0, 1.0);
    float2 uvGreen = clamp(UV - dir * distortion * 0.5, 0.0, 1.0);
    float2 uvBlue = clamp(UV + dir * distortion * 1.0, 0.0, 1.0);
    
    // ѕреобразование в текстурные координаты с проверкой границ
    uint2 texCoordRed = uint2(uvRed * gRenderTargetSize);
    uint2 texCoordGreen = uint2(uvGreen * gRenderTargetSize);
    uint2 texCoordBlue = uint2(uvBlue * gRenderTargetSize);
    
    // ѕроверка на выход за пределы текстуры
    int3 coordRed = int3(clamp(texCoordRed, 0, gRenderTargetSize - 1), 0);
    int3 coordGreen = int3(clamp(texCoordGreen, 0, gRenderTargetSize - 1), 0);
    int3 coordBlue = int3(clamp(texCoordBlue, 0, gRenderTargetSize - 1), 0);
    
    // «агрузка данных с защитой от выхода за границы
    float red = gDiffuseMap.Load(coordRed).r;
    float green = gDiffuseMap.Load(coordGreen).g;
    float blue = gDiffuseMap.Load(coordBlue).b;
   
    
    return float4(red, green, blue, 1.0);
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
   
    Color.xyz = pow(saturate(Color.xyz), 1.0 / 2.2);
    return Color;
}
