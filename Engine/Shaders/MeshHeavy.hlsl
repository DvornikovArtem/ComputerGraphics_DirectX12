// MeshHeavy.hlsl

cbuffer MeshPassCB : register(b0)
{
    float4x4 gViewProj;
    float2 gGridOrigin;
    float2 gGridScale;
};

struct MeshVSOut
{
    float4 Pos : SV_Position;
    float3 Color : COLOR0;
};

#define MESH_MAX_VERTS    4
#define MESH_MAX_PRIMS    2

[outputtopology("triangle")]

[numthreads(1, 1, 1)]
void MeshMain(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex,
    out vertices MeshVSOut outVerts[MESH_MAX_VERTS],
    out indices uint3 outPrims[MESH_MAX_PRIMS])
{
    SetMeshOutputCounts(MESH_MAX_VERTS, MESH_MAX_PRIMS);
    
    const uint GRID_SIZE = 128;
    
    float2 cell = (float2(groupId.x, groupId.y) / (GRID_SIZE - 1).xx) * 2.0f - 1.0f;
    
    float cellSize = 0.015f;
    
    float3 centerWorld = float3(cell.x, 0.0f, cell.y);
    
    float3 color = 0.5f + 0.5f * float3(cell.x, abs(cell.x * cell.y), cell.y);
    
    float3 p0 = centerWorld + float3(-cellSize, 0.0f, -cellSize);
    float3 p1 = centerWorld + float3(cellSize, 0.0f, -cellSize);
    float3 p2 = centerWorld + float3(cellSize, 0.0f, cellSize);
    float3 p3 = centerWorld + float3(-cellSize, 0.0f, cellSize);

    float4 clip0 = mul(float4(p0, 1.0f), gViewProj);
    float4 clip1 = mul(float4(p1, 1.0f), gViewProj);
    float4 clip2 = mul(float4(p2, 1.0f), gViewProj);
    float4 clip3 = mul(float4(p3, 1.0f), gViewProj);

    outVerts[0].Pos = clip0;
    outVerts[0].Color = color;

    outVerts[1].Pos = clip1;
    outVerts[1].Color = color;

    outVerts[2].Pos = clip2;
    outVerts[2].Color = color;

    outVerts[3].Pos = clip3;
    outVerts[3].Color = color;
    
    outPrims[0] = uint3(0, 1, 2);
    outPrims[1] = uint3(0, 2, 3);
}

float4 MeshPS(MeshVSOut input) : SV_Target0
{
    return float4(input.Color, 1.0f);
}