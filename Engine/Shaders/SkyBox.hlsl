#include "CBufferStructures.hlsl"

TextureCube DiffuseMap : register(t0);

SamplerState samPointWrap : register(s0);
SamplerState samPointClamp : register(s1);
SamplerState samLinearWrap : register(s2);
SamplerState samLinearClamp : register(s3);
SamplerState samAnisotropicWrap : register(s4);
SamplerState samAnisotropicClamp : register(s5);

ConstantBuffer<ObjectCB> cbObject : register(b0);

ConstantBuffer<MainPassCB> cbMainPass : register(b1);

struct VS_IN
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
};

struct VS_OUT_PS_IN
{
	float4 PosH : SV_POSITION;
    float3 PosL : POSITION;
};
 
VS_OUT_PS_IN VS(VS_IN vin)
{
    VS_OUT_PS_IN vout;

	// Use local vertex position as cubemap lookup vector.
	vout.PosL = vin.PosL;
	
    float4 posW = mul(float4(vin.PosL, 1.0f), cbObject.World);

	// Always center sky about camera.
    posW.xyz += cbMainPass.CameraPos;
	// Set z = w so that z/w = 1 (i.e., skydome always on far plane).
    vout.PosH = mul(posW, cbMainPass.ViewProj).xyww;
	
	return vout;
}

float4 PS(VS_OUT_PS_IN pin) : SV_Target
{
    return DiffuseMap.Sample(samLinearWrap, pin.PosL);
}

