#include "CBufferStructures.hlsl"

Texture2D   CurrFrame     : register(t0);
Texture2D   PrevFrame    : register(t1);
Texture2D   DepthMap      : register(t2);
Texture2D   MotionVectors : register(t3);

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

float4 PS(VertexOut pin) : SV_Target
{
    uint2 TexelCoord = pin.PosH.xy;

    float2 MotionVector = MotionVectors.Load(int3(TexelCoord, 0)).xy;
    float MotionLength = length(MotionVector);
    
    float2 PrevTexelCoord = TexelCoord + MotionVector;
    float4 CurrFrameColor = CurrFrame.Load(int3(TexelCoord, 0));
    float4 PrevFrameColor = CurrFrameColor;
    
    bool IsPrevUVValid = all(PrevTexelCoord >= 0 && PrevTexelCoord < cbMainPass.RenderTargetSize);
    if (IsPrevUVValid)
    {
        PrevFrameColor = PrevFrame.Load(int3(PrevTexelCoord, 0));
        
        // Color clamping
        float4 minColor = CurrFrameColor;
        float4 maxColor = CurrFrameColor;
        
        for (int x = -1; x <= 1; x++)
        {
            for (int y = -1; y <= 1; y++)
            {
                uint2 neighborCoord = TexelCoord + uint2(x, y);
                if (all(neighborCoord >= 0 && neighborCoord < cbMainPass.RenderTargetSize))
                {
                    float4 neighborColor = CurrFrame.Load(int3(neighborCoord, 0));
                    minColor = min(minColor, neighborColor);
                    maxColor = max(maxColor, neighborColor);
                }
            }
        }
        
        PrevFrameColor = clamp(PrevFrameColor, minColor, maxColor);
        
        float BlendFactor = 0.9 * saturate(1.0 - MotionLength / 50.0); // more movement = less influence
        return lerp(CurrFrameColor, PrevFrameColor, BlendFactor);
    }
    
    return CurrFrameColor;
}