#include "CBufferStructures.hlsl"

Texture2D DiffuseMap : register(t0);
Texture2D NormalMap : register(t1);
Texture2D HeightMap : register(t2);

SamplerState samPointWrap : register(s0);
SamplerState samPointClamp : register(s1);
SamplerState samLinearWrap : register(s2);
SamplerState samLinearClamp : register(s3);
SamplerState samAnisotropicWrap : register(s4);
SamplerState samAnisotropicClamp : register(s5);

ConstantBuffer<ObjectCB> cbObject : register(b0);

ConstantBuffer<LightCB> cbLight : register(b1);

ConstantBuffer<MaterialCB> cbMaterial : register(b2);

struct VS_INPUT
{
    float3 Pos : POSITION;
    float2 TexC : TEXCOORD;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
};

struct DS_VS_OUTPUT_PS_INPUT
{
    float3 PosW    : POSITION;
    float2 TexC    : TEXCOORD;
    float3 Normal  : NORMAL;
    float3 Tangent : TANGENT;
};

DS_VS_OUTPUT_PS_INPUT VS(VS_INPUT vin)
{
    DS_VS_OUTPUT_PS_INPUT vout = (DS_VS_OUTPUT_PS_INPUT) 0.0f;
	
    float4 posW = mul(float4(vin.Pos, 1.0f), cbObject.World);
    vout.PosW = posW.xyz;

    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
    vout.Normal = normalize(mul(vin.Normal, (float3x3) cbObject.World));
    
    vout.Tangent = normalize(mul(vin.Tangent, (float3x3) cbObject.World));
	
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), cbObject.TexTransform);
    vout.TexC = mul(texC, cbMaterial.MatTransform).xy;

    return vout;
}

struct GS_OUT
{
    float4 PosCS : SV_Position;
    uint ArrInd : SV_RenderTargetArrayIndex;
    float2 TexC : TEXCOORD;
};

[instance(6)]
[maxvertexcount(3)]
void GS(triangle DS_VS_OUTPUT_PS_INPUT p[3], in uint id : SV_GSInstanceID, inout TriangleStream<GS_OUT> stream)
{
    if(cbLight.lightData.LightType == 0)
    {
        // draw 5 cascades for directional lights
        
        //first 5 instances only
        if (id > 4)
            return;
        
        for (int i = 0; i < 3; i++)
        {
            GS_OUT Out;
            Out.PosCS = mul(float4(p[i].PosW.xyz, 1.f), mul(cbLight.View[id], cbLight.Proj[id]));
            Out.TexC = p[i].TexC;
            Out.ArrInd = id;
            stream.Append(Out);
        }
    }
    else if (cbLight.lightData.LightType == 1)
    {
        // draw 6 sides of shadow map cube for point lights
        for (int i = 0; i < 3; i++)
        {
            GS_OUT Out;
            Out.PosCS = mul(float4(p[i].PosW.xyz, 1.f), mul(cbLight.View[id], cbLight.Proj[id]));
            Out.TexC = p[i].TexC;
            Out.ArrInd = id;
            stream.Append(Out);
        }
    }
    else if (cbLight.lightData.LightType == 2)
    {
        //no cascades for spot lights
        
        //first instance only
        if (id != 0)
            return;
        
        for (int i = 0; i < 3; i++)
        {
            GS_OUT Out;
            Out.PosCS = mul(float4(p[i].PosW.xyz, 1.f), mul(cbLight.View[0], cbLight.Proj[0]));
            Out.TexC = p[i].TexC;
            Out.ArrInd = 0;
            stream.Append(Out);
        }
    }
}

//might need to add proper PS here
void PS(GS_OUT pin)
{
    
    float2 uv = pin.TexC;
    
    float4 diffusealbedo = DiffuseMap.Sample(samAnisotropicWrap, uv) * cbMaterial.DiffuseAlbedo;
    
	// discard pixel if texture alpha < 0.1.  we do this test as soon 
	// as possible in the shader so that we can potentially exit the
	// shader early, thereby skipping the rest of the shader code.
    clip(diffusealbedo.a - 0.1f);
}


