#include "CBufferStructures.hlsl"

ConstantBuffer<MainPassCB> cbMainPass : register(b0);

static const float GRID_Y = 0.0;
static const float CELL_MINOR = 1.0;
static const float MAJOR_STEP = 10.0;
static const float MEGA_STEP = 100.0;

static const float TH_PX_MINOR = 1.2;
static const float TH_PX_MAJOR = 2.0;
static const float TH_PX_MEGA = 2.2;
static const float TH_PX_HILITE = 2.2;
static const int HILITE_MINOR_NEI = 3;

static const float DENSITY_FADE_START_MINOR = 0.15;
static const float DENSITY_FADE_END_MINOR = 0.40;
static const float DENSITY_FADE_START_MAJOR = 0.08;
static const float DENSITY_FADE_END_MAJOR = 0.20;
static const float DENSITY_FADE_START_MEGA = 0.02;
static const float DENSITY_FADE_END_MEGA = 0.06;

static const float REF_CELL = 5.0;

static const float T_FADE_START = 10000.0;
static const float T_FADE_END = 40000.0;
static const float T_CULL_MAX = 60000.0;

static const float3 COL_GRID = float3(0.5, 0.5, 0.5);
static const float3 COL_HORZ = float3(0.20, 0.85, 0.35);
static const float3 COL_VERT = float3(0.95, 0.25, 0.25);

#define GRID_STOCHASTIC 1

struct VSOut
{
    float4 pos : SV_Position;
    float2 ndc : TEXCOORD0;
};

VSOut SceneGridVS(uint vid : SV_VertexID)
{
    float2 p = float2((vid << 1) & 2, vid & 2);
    p = p * 2.0 + float2(-1.0, -1.0);
    VSOut o;
    o.pos = float4(p, 0.0, 1.0);
    o.ndc = p;
    return o;
}

float3 RayDirFromNDC(float2 ndc)
{
    float4 farW = mul(float4(ndc, 1.0, 1.0), cbMainPass.InvViewProj);
    farW.xyz /= farW.w;
    return normalize(farW.xyz - cbMainPass.EyePosW);
}

float smstep(float a, float b, float x)
{
    float t = saturate((x - a) / max(b - a, 1e-6));
    return t * t * (3.0 - 2.0 * t);
}

float hash_w(float2 p, float time)
{
    float2 i = floor(p);
    float h = dot(i, float2(127.1, 311.7)) + time * 0.0;
    return frac(sin(h) * 43758.5453);
}

float cov_union(float a, float b)
{
    return 1.0 - (1.0 - a) * (1.0 - b);
}

float HiliteWeightAroundNearestMajorOnMinor(float uMinor, float uCamMinor, float majorStepMinor, int neiMinor)
{
    float camNearestMajor = round(uCamMinor / majorStepMinor) * majorStepMinor;
    float deltaMinor = abs(uMinor - camNearestMajor);
    float reach = (float) neiMinor + 0.5;
    return saturate((reach - deltaMinor) / reach);
}

struct PSOut
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

PSOut SceneGridPS(VSOut i)
{
    PSOut o;
    o.color = 0;
    o.depth = 1;
    
    float3 ro = cbMainPass.EyePosW;
    float3 rd = RayDirFromNDC(i.ndc);
    if (rd.y >= -1e-6)
        clip(-1);

    float t = (GRID_Y - ro.y) / rd.y;
    if (t <= 0.0 || t > T_CULL_MAX)
        clip(-1);

    float3 Pw = ro + rd * t;
    float2 uv = Pw.xz / CELL_MINOR;
    
    float fw_x = fwidth(uv.x);
    float fw_y = fwidth(uv.y);
    
    float fadeScale = REF_CELL / CELL_MINOR;
    
    float startMinor = DENSITY_FADE_START_MINOR * fadeScale;
    float endMinor = DENSITY_FADE_END_MINOR * fadeScale;
    float wMinorFade = 1.0 - smstep(startMinor, endMinor, max(fw_x, fw_y));

    float2 uvMajor = uv / MAJOR_STEP;
    float fw_Mx = fw_x / MAJOR_STEP;
    float fw_My = fw_y / MAJOR_STEP;
    float startMajor = DENSITY_FADE_START_MAJOR * fadeScale;
    float endMajor = DENSITY_FADE_END_MAJOR * fadeScale;
    float wMajorFade = 1.0 - smstep(startMajor, endMajor, max(fw_Mx, fw_My));

    float2 uvMega = uv / MEGA_STEP;
    float fw_Mgx = fw_x / MEGA_STEP;
    float fw_Mgy = fw_y / MEGA_STEP;
    float startMega = DENSITY_FADE_START_MEGA * fadeScale;
    float endMega = DENSITY_FADE_END_MEGA * fadeScale;
    float wMegaFade = 1.0 - smstep(startMega, endMega, max(fw_Mgx, fw_Mgy));
    
    float zView = mul(float4(Pw, 1.0), cbMainPass.View).z;
    float depthVS = abs(zView);
    float thicknessK = 1.0 - smstep(T_FADE_START, T_FADE_END, depthVS);
    thicknessK = max(thicknessK, 1e-3);
    
    // MINOR
    float fu_x = frac(uv.x), fu_y = frac(uv.y);
    float d_x = min(fu_x, 1.0 - fu_x);
    float d_y = min(fu_y, 1.0 - fu_y);

    float w_xp = max(fw_x * (TH_PX_MINOR * 0.5) * thicknessK, 1e-6);
    float w_yp = max(fw_y * (TH_PX_MINOR * 0.5) * thicknessK, 1e-6);

    float mV = saturate(1.0 - d_x / w_xp);
    float mH = saturate(1.0 - d_y / w_yp);
    float minorMask = cov_union(mH, mV) * wMinorFade;

    // MAJOR
    float fu_Mx = frac(uvMajor.x), fu_My = frac(uvMajor.y);
    float d_Mx = min(fu_Mx, 1.0 - fu_Mx);
    float d_My = min(fu_My, 1.0 - fu_My);

    float w_Mxp = max(fw_Mx * (TH_PX_MAJOR * 0.5) * thicknessK, 1e-6);
    float w_Myp = max(fw_My * (TH_PX_MAJOR * 0.5) * thicknessK, 1e-6);

    float M_V = saturate(1.0 - d_Mx / w_Mxp);
    float M_H = saturate(1.0 - d_My / w_Myp);
    float majorMask = cov_union(M_V, M_H) * wMajorFade;

    // MEGA
    float fu_Mgx = frac(uvMega.x), fu_Mgy = frac(uvMega.y);
    float d_Mgx = min(fu_Mgx, 1.0 - fu_Mgx);
    float d_Mgy = min(fu_Mgy, 1.0 - fu_Mgy);

    float w_Mgxp = max(fw_Mgx * (TH_PX_MEGA * 0.5) * thicknessK, 1e-6);
    float w_Mgyp = max(fw_Mgy * (TH_PX_MEGA * 0.5) * thicknessK, 1e-6);

    float Mega_V = saturate(1.0 - d_Mgx / w_Mgxp);
    float Mega_H = saturate(1.0 - d_Mgy / w_Mgyp);
    float megaMask = cov_union(Mega_V, Mega_H) * wMegaFade;
    
    float gridMask = 1.0;
    gridMask *= (1.0 - minorMask);
    gridMask *= (1.0 - majorMask);
    gridMask *= (1.0 - megaMask);
    gridMask = 1.0 - gridMask;
    
    float2 uCamMinor = ro.xz / CELL_MINOR;

    float wHL_y = max(fw_y * (TH_PX_HILITE * 0.5) * thicknessK, 1e-6);
    float baseHL_H = saturate(1.0 - d_y / wHL_y);
    float wH = HiliteWeightAroundNearestMajorOnMinor(uv.y, uCamMinor.y, MAJOR_STEP, HILITE_MINOR_NEI);
    float HL_H = baseHL_H * wH * wMinorFade;

    float wHL_x = max(fw_x * (TH_PX_HILITE * 0.5) * thicknessK, 1e-6);
    float baseHL_V = saturate(1.0 - d_x / wHL_x);
    float wV = HiliteWeightAroundNearestMajorOnMinor(uv.x, uCamMinor.x, MAJOR_STEP, HILITE_MINOR_NEI);
    float HL_V = baseHL_V * wV * wMinorFade;
    
    float alphaGeom = max(gridMask, max(HL_H, HL_V));
    
#if GRID_STOCHASTIC
    float fadeActivity = 1.0 - thicknessK;
    fadeActivity = max(fadeActivity, 1.0 - wMinorFade);
    fadeActivity = saturate(fadeActivity);

    float noise = hash_w(Pw.xz, cbMainPass.TotalTime);
    float alphaStoch = alphaGeom;
    
    float k = fadeActivity;
    float a2c = alphaStoch - k * (noise - 0.5) / 255.0;

    clip(a2c - 1e-3);
    float finalAlpha = alphaStoch;
#else
    float finalAlpha = alphaGeom;
    clip(finalAlpha - 1e-3);
#endif
    
    float3 rgb = COL_GRID;
    rgb = lerp(rgb, COL_HORZ, HL_H);
    rgb = lerp(rgb, COL_VERT, HL_V);
    
    float4 clipPos = mul(float4(Pw, 1.0), cbMainPass.ViewProj);
    o.depth = clipPos.z / clipPos.w;

    o.color = float4(rgb, finalAlpha * 0.85);
    return o;
}
