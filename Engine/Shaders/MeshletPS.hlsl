//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

/*
float4 main(VertexOut input) : SV_TARGET
{
    float ambientIntensity = 0.1;
    float3 lightColor = float3(1, 1, 1);
    float3 lightDir = -normalize(float3(1, -1, 1));

    float3 diffuseColor;
    float shininess;
    if (Globals.DrawMeshlets)
    {
        uint meshletIndex = input.MeshletIndex;
        diffuseColor = float3(
            float(meshletIndex & 1),
            float(meshletIndex & 3) / 4,
            float(meshletIndex & 7) / 8);
        shininess = 16.0;
    }
    else
    {
        diffuseColor = 0.8;
        shininess = 64.0;
    }

    float3 normal = normalize(input.Normal);

    // Do some fancy Blinn-Phong shading!
    float cosAngle = saturate(dot(normal, lightDir));
    float3 viewDir = -normalize(input.PositionVS);
    float3 halfAngle = normalize(lightDir + viewDir);

    float blinnTerm = saturate(dot(normal, halfAngle));
    blinnTerm = cosAngle != 0.0 ? blinnTerm : 0.0;
    blinnTerm = pow(blinnTerm, shininess);

    float3 finalColor = (cosAngle + blinnTerm + ambientIntensity) * diffuseColor;

    return float4(finalColor, 1);
}*/

struct Constants
{
    float4x4 World;
    float4x4 WorldView;
    float4x4 WorldViewProj;
    uint DrawMeshlets;
};

struct VertexOut
{
    float4 PositionHS   : SV_Position;
    float3 PositionVS   : POSITION0;
    float3 Normal       : NORMAL0;
    uint MeshletIndex   : COLOR0;
};

ConstantBuffer<Constants> Globals : register(b0);

struct PSOut
{
    float4 Diffuse              : SV_Target0;
    float4 DepthStencils        : SV_Target1;
    float4 Normal               : SV_Target2;
    float4 MatFresnelRoughness  : SV_Target3;
    float2 Velocity             : SV_Target4;
    float4 ObjectOutlines       : SV_Target5;
};

float3 EncodeNormalTo01(float3 n)
{
    // [-1..1] -> [0..1]
    return n * 0.5f + 0.5f;
}

PSOut main(VertexOut input)
{
    PSOut o;
    
    float3 diffuseColor;
    if (Globals.DrawMeshlets)
    {
        uint meshletIndex = input.MeshletIndex;
        diffuseColor = float3(
            float(meshletIndex & 1),
            float(meshletIndex & 3) / 4,
            float(meshletIndex & 7) / 8
        );
    }
    else
    {
        diffuseColor = float3(0.8f, 0.8f, 0.8f);
    }
    
    o.Diffuse = float4(diffuseColor, 1.0f);
    
    float linearDepth = -input.PositionVS.z;
    o.DepthStencils = float4(linearDepth, linearDepth, linearDepth, 1.0f);
    
    float3 n = normalize(input.Normal);
    o.Normal = float4(EncodeNormalTo01(n), 1.0f);
    
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    float roughness = 0.5f;
    o.MatFresnelRoughness = float4(F0, roughness);
    
    o.Velocity = float2(0.0f, 0.0f);
    o.ObjectOutlines = float4(0.0f, 0.0f, 0.0f, 0.0f);

    return o;
}