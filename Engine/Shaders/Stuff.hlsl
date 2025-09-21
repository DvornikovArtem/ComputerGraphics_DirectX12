#pragma once
#ifndef PASS_CB_REGISTER
#define PASS_CB_REGISTER b1 // default register
#endif
#ifndef PASS_CB_SPACE
#define PASS_CB_SPACE space0 // default space
#endif


struct PassCB
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;

    float3 gEyePosW;
    float _pad0;

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
    float2 _pad1;

    float4 Decals[3];
};

// Объект CBV, регистр задаётся макросом:
ConstantBuffer<PassCB> cbPass : register(PASS_CB_REGISTER, PASS_CB_SPACE);