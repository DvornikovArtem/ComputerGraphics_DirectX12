// MarchingCubesCS.hlsl
//#include "triTable_packed.hlsl"

/*
struct VertexOut {
    float3 Pos;    float _pad0;   // 16
    float3 Normal; float _pad1;   // 16 (32)
    float2 TexC;   float2 _pad2;  // 8 + 8 = 16 (48)
    float3 Tangent; float _pad3;  // 16 (64)
};*/

struct VertexOut
{
    float3 Pos;
    float _pad0; // 16
    float3 Normal;
    float _pad1; // 16 (32)
    float2 TexC;
    float2 _pad2; // 8 + 8 = 16 (48)
    float3 Tangent;
    float _pad3; // 16 (64)
};

Texture3D<float> gDensity : register(t0);
SamplerState gSampLinear  : register(s0);
//AppendStructuredBuffer<VertexOut> gVertices : register(u0);
// RWByteAddressBuffer gDrawArgs : register(u1);
RWStructuredBuffer<VertexOut> gVertices : register(u0);
RWByteAddressBuffer gVertCount : register(u1);

cbuffer VoxelChunkCB : register(b0)
{
    float3 gWorldOrigin;
    float gVoxelSize;
    uint gDimX;
    uint gDimY;
    uint gDimZ;
    float gIsoLevel;
};

cbuffer MCTables : register(b1)
{
    int4 edgeTable[256];
    int4 triTable[256][4];
};


float3 WorldFromGrid(uint3 c)
{
    return gWorldOrigin + (float3(c) * gVoxelSize);
}


float3 CalcGradient(float3 wp)
{
    /*float3 texDim = float3(gDimX, gDimY, gDimZ);
    float3 uvw = (wp - gWorldOrigin) / (gVoxelSize * (texDim - 1.0));
    float3 o = (1.0 / (texDim - 1.0));

    float dx = gDensity.SampleLevel(gSampLinear, saturate(uvw + float3(o.x,0,0)), 0)
             - gDensity.SampleLevel(gSampLinear, saturate(uvw - float3(o.x,0,0)), 0);
    float dy = gDensity.SampleLevel(gSampLinear, saturate(uvw + float3(0,o.y,0)), 0)
             - gDensity.SampleLevel(gSampLinear, saturate(uvw - float3(0,o.y,0)), 0);
    float dz = gDensity.SampleLevel(gSampLinear, saturate(uvw + float3(0,0,o.z)), 0)
             - gDensity.SampleLevel(gSampLinear, saturate(uvw - float3(0,0,o.z)), 0);

    float3 n = float3(dx,dy,dz);
    if (dot(n,n) < 1e-12) n = float3(0,1,0);
    return -normalize(n);*/
    
    
    /*float3 texDim = float3(gDimX, gDimY, gDimZ);
    float3 localPos = (wp - gWorldOrigin) / gVoxelSize;
    
    if (any(localPos < 0.0) || any(localPos >= texDim - 1.0))
        return float3(0, 1, 0);
    
    float3 uvw = localPos / (texDim - 1.0);
    float3 o = (1.0 / (texDim - 1.0));

    float dx = gDensity.SampleLevel(gSampLinear, uvw + float3(o.x, 0, 0), 0)
             - gDensity.SampleLevel(gSampLinear, uvw - float3(o.x, 0, 0), 0);
    float dy = gDensity.SampleLevel(gSampLinear, uvw + float3(0, o.y, 0), 0)
             - gDensity.SampleLevel(gSampLinear, uvw - float3(0, o.y, 0), 0);
    float dz = gDensity.SampleLevel(gSampLinear, uvw + float3(0, 0, o.z), 0)
             - gDensity.SampleLevel(gSampLinear, uvw - float3(0, 0, o.z), 0);

    float3 n = float3(dx, dy, dz);
    float len2 = dot(n, n);
    if (len2 < 1e-12)
        return float3(0, 1, 0);
    return -normalize(n);*/
    
    
    
    
    /*
    float3 localPos = (wp - gWorldOrigin) / gVoxelSize;
    int3 gridPos = int3(round(localPos));
    
    if (any(gridPos < 1) || any(gridPos.x >= int(gDimX) - 1) ||
        any(gridPos.y >= int(gDimY) - 1) || any(gridPos.z >= int(gDimZ) - 1))
        return float3(0, 1, 0);
    
    float dx = gDensity.Load(int4(gridPos.x + 1, gridPos.y, gridPos.z, 0))
             - gDensity.Load(int4(gridPos.x - 1, gridPos.y, gridPos.z, 0));
    float dy = gDensity.Load(int4(gridPos.x, gridPos.y + 1, gridPos.z, 0))
             - gDensity.Load(int4(gridPos.x, gridPos.y - 1, gridPos.z, 0));
    float dz = gDensity.Load(int4(gridPos.x, gridPos.y, gridPos.z + 1, 0))
             - gDensity.Load(int4(gridPos.x, gridPos.y, gridPos.z - 1, 0));

    float3 n = float3(dx, dy, dz);
    float len2 = dot(n, n);
    if (len2 < 1e-12)
        return float3(0, 1, 0);
    return -normalize(n);
    */
    
    
    
    
    
    float3 localPos = (wp - gWorldOrigin) / gVoxelSize;
    int3 g = int3(round(localPos));
    
    int x0 = clamp(g.x - 1, 0, int(gDimX) - 1);
    int x1 = clamp(g.x + 1, 0, int(gDimX) - 1);
    int y0 = clamp(g.y - 1, 0, int(gDimY) - 1);
    int y1 = clamp(g.y + 1, 0, int(gDimY) - 1);
    int z0 = clamp(g.z - 1, 0, int(gDimZ) - 1);
    int z1 = clamp(g.z + 1, 0, int(gDimZ) - 1);
    
    float dx = gDensity.Load(int4(x1, g.y, g.z, 0)) - gDensity.Load(int4(x0, g.y, g.z, 0));
    float dy = gDensity.Load(int4(g.x, y1, g.z, 0)) - gDensity.Load(int4(g.x, y0, g.z, 0));
    float dz = gDensity.Load(int4(g.x, g.y, z1, 0)) - gDensity.Load(int4(g.x, g.y, z0, 0));

    float3 n = float3(dx, dy, dz);
    return (dot(n, n) > 1e-12) ? normalize(n) : float3(0, 1, 0);
}

float3 EdgeLerp(float3 p0, float3 p1, float d0, float d1, float iso){
    //float t = saturate((iso - d0)/max(d1-d0,1e-6));
    //return lerp(p0,p1,t);
    
    
    /*
    float denom = (d1 - d0);
    if (abs(denom) < 1e-8)return 0.5 * (p0 + p1);
    float t = (iso - d0) / denom;
    return lerp(p0, p1, t);
    */
    
    
    /*
    float denom = (d1 - d0);
    if (abs(denom) < 1e-6) 
        return 0.5 * (p0 + p1);
    
    float t = (iso - d0) / denom;
    t = saturate(t);
    return lerp(p0, p1, t);
    */
    
    
    /*
    float t = (iso - d0) / (d1 - d0 + 1e-6f);
    t = saturate(t);
    return lerp(p0, p1, t);
    */
    
    
    /*
    if (abs(iso - d0) < 1e-6)
        return p0;
    if (abs(iso - d1) < 1e-6)
        return p1;
    
    if (abs(d0 - d1) < 1e-6)
        return p0;
    
    float mu = (iso - d0) / (d1 - d0);
    
    float3 p;
    p.x = p0.x + mu * (p1.x - p1.x);
    p.y = p0.y + mu * (p1.y - p1.y);
    p.z = p0.z + mu * (p1.z - p1.z);
    return p;
    */
    
    
    
    float denom = d1 - d0;
    float t = (abs(denom) < 1e-6f) ? 0.5f : (iso - d0) / denom;
    t = saturate(t);
    return lerp(p0, p1, t);
    
}

float3 TangentFromNormal(float3 n){
    float sign = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + n.z);
    float b = n.x * n.y * a;
    return normalize(float3(1.0 + sign * n.x * n.x * a, sign * b, -sign * n.x));
}

int GetTriPacked(uint cube, uint k)
{
    uint g = k / 4;
    
    // Take two right bits of 'k'
    uint idx = k & 3;
    return triTable[cube][g][idx];
}


float3 TetEdgePoint(int2 e, int4 tet, float3 p8[8], float d8[8], float iso)
{
    int a = tet[e.x];
    int b = tet[e.y];
    return EdgeLerp(p8[a], p8[b], d8[a], d8[b], iso);
}


#define DEBUG_XZ_PLANES 0


[numthreads(8, 8, 8)]
void CS(uint3 cellId : SV_DispatchThreadID)
{
    if (cellId.x >= gDimX - 1 || cellId.y >= gDimY - 1 || cellId.z >= gDimZ - 1) return;

    /*VertexOut V = (VertexOut) 0;
    V.Pos = float3(1, 2, 3);
    V.Normal = float3(4, 5, 6);
    V.TexC = float2(7, 8);
    V.Tangent = float3(9, 10, 11);

    uint i;
    gVertCount.InterlockedAdd(0, 1, i);
    gVertices[i] = V;*/
    
    /*
    uint3 c000 = cellId;
    uint3 c100 = cellId + uint3(1,0,0);
    uint3 c110 = cellId + uint3(1,1,0);
    uint3 c010 = cellId + uint3(0,1,0);
    uint3 c001 = cellId + uint3(0,0,1);
    uint3 c101 = cellId + uint3(1,0,1);
    uint3 c111 = cellId + uint3(1,1,1);
    uint3 c011 = cellId + uint3(0,1,1);
    */
    
    
    
    uint3 c000 = cellId + uint3(0, 0, 0); // 0
    uint3 c100 = cellId + uint3(1, 0, 0); // 1
    uint3 c110 = cellId + uint3(1, 1, 0); // 2
    uint3 c010 = cellId + uint3(0, 1, 0); // 3
    uint3 c001 = cellId + uint3(0, 0, 1); // 4
    uint3 c101 = cellId + uint3(1, 0, 1); // 5
    uint3 c111 = cellId + uint3(1, 1, 1); // 6
    uint3 c011 = cellId + uint3(0, 1, 1); // 7
    
    
    
    /*
    uint3 c000 = cellId + uint3(0, 0, 0); // 0
    uint3 c100 = cellId + uint3(1, 0, 0); // 1
    uint3 c010 = cellId + uint3(0, 1, 0); // 2
    uint3 c110 = cellId + uint3(1, 1, 0); // 3
    uint3 c001 = cellId + uint3(0, 0, 1); // 4
    uint3 c101 = cellId + uint3(1, 0, 1); // 5
    uint3 c011 = cellId + uint3(0, 1, 1); // 6
    uint3 c111 = cellId + uint3(1, 1, 1); // 7
    */
    

    
    float3 p[8] = {
        WorldFromGrid(c000),
        WorldFromGrid(c100),
        WorldFromGrid(c110),
        WorldFromGrid(c010),
        WorldFromGrid(c001),
        WorldFromGrid(c101),
        WorldFromGrid(c111),
        WorldFromGrid(c011)
    };

    float d[8] = {
        gDensity.Load(int4(c000,0)),
        gDensity.Load(int4(c100,0)),
        gDensity.Load(int4(c110,0)),
        gDensity.Load(int4(c010,0)),
        gDensity.Load(int4(c001,0)),
        gDensity.Load(int4(c101,0)),
        gDensity.Load(int4(c111,0)),
        gDensity.Load(int4(c011,0))
    };
    
    
    /*
    float3 p[8] =
    {
        WorldFromGrid(c000),
        WorldFromGrid(c100),
        WorldFromGrid(c010),
        WorldFromGrid(c110),
        WorldFromGrid(c001),
        WorldFromGrid(c101),
        WorldFromGrid(c011),
        WorldFromGrid(c111)
    };

    float d[8] =
    {
        gDensity.Load(int4(c000, 0)),
        gDensity.Load(int4(c100, 0)),
        gDensity.Load(int4(c010, 0)),
        gDensity.Load(int4(c110, 0)),
        gDensity.Load(int4(c001, 0)),
        gDensity.Load(int4(c101, 0)),
        gDensity.Load(int4(c011, 0)),
        gDensity.Load(int4(c111, 0))
    };
    */

    int cubeIndex = 0;
    [unroll] for (int i = 0; i < 8; i++) if (d[i] < gIsoLevel) cubeIndex |= (1 << i);
    
    if (cubeIndex == 0 || cubeIndex == 255) return;
    
    bool allValid = true;
    
    [unroll]
    for (int q = 0; q < 8; q++)
    {
        if (isnan(d[q]) || isinf(d[q]))
        {
            allValid = false;
            break;
        }
    }
    if (!allValid) return;

    
    #if DEBUG_XZ_PLANES
{
        float3 wp000 = WorldFromGrid(cellId + uint3(0, 0, 0));
        float3 wp100 = WorldFromGrid(cellId + uint3(1, 0, 0));
        float3 wp101 = WorldFromGrid(cellId + uint3(1, 0, 1));
        float3 wp001 = WorldFromGrid(cellId + uint3(0, 0, 1));

        float yMid = wp000.y + 0.5f * gVoxelSize;

        float3 P0 = float3(wp000.x, yMid, wp000.z);
        float3 P1 = float3(wp100.x, yMid, wp100.z);
        float3 P2 = float3(wp101.x, yMid, wp101.z);
        float3 P3 = float3(wp001.x, yMid, wp001.z);
        
        float3 N = float3(0, 1, 0);
        float3 T = TangentFromNormal(N);

        VertexOut A = (VertexOut) 0;
        VertexOut B = (VertexOut) 0;
        VertexOut C = (VertexOut) 0;
        
        A.Pos = P0;
        A.Normal = N;
        A.Tangent = T;
        A.TexC = 0;
        B.Pos = P2;
        B.Normal = N;
        B.Tangent = T;
        B.TexC = 0;
        C.Pos = P1;
        C.Normal = N;
        C.Tangent = T;
        C.TexC = 0;

        uint baseVert;
        gVertCount.InterlockedAdd(0, 3, baseVert);
        gVertices[baseVert + 0] = A;
        gVertices[baseVert + 1] = B;
        gVertices[baseVert + 2] = C;
        
        A.Pos = P0;
        B.Pos = P3;
        C.Pos = P2;

        gVertCount.InterlockedAdd(0, 3, baseVert);
        gVertices[baseVert + 0] = A;
        gVertices[baseVert + 1] = B;
        gVertices[baseVert + 2] = C;

        return;
    }
#endif
    
    int edges = edgeTable[cubeIndex].x;
    if (edges == 0) return;
    
    float3 v[12];
    [unroll] for (int h = 0; h < 12; ++h)
    {
        v[h].x = 0;
        v[h].y = 0;
        v[h].z = 0;
    }
    
    
    
    
    const int2 edgeToCorners[12] =
    {
        int2(0, 1), int2(1, 2), int2(2, 3), int2(3, 0),
        int2(4, 5), int2(5, 6), int2(6, 7), int2(7, 4),
        int2(0, 4), int2(1, 5), int2(2, 6), int2(3, 7)
    };
    
    
    
    /*
    const int2 edgeToCorners[12] =
    {
        int2(0, 1),
        int2(1, 3),
        int2(3, 2),
        int2(2, 0),
        int2(4, 5),
        int2(5, 7),
        int2(7, 6),
        int2(6, 4),
        int2(0, 4),
        int2(1, 5),
        int2(3, 7),
        int2(2, 6)
    };
    */
    
    
    [unroll]
    for (int e = 0; e < 12; e++)
    {
        if (edges & (1 << e))
        {
            int2 c = edgeToCorners[e];
            v[e] = EdgeLerp(p[c.x], p[c.y], d[c.x], d[c.y], gIsoLevel);
        }
    }
    

    [unroll]
    for (int t = 0; t < 5; t++)
    {
        int k0 = 3 * t + 0;
        int k1 = 3 * t + 1;
        int k2 = 3 * t + 2;
        int i0 = GetTriPacked(cubeIndex, k0);
        int i1 = GetTriPacked(cubeIndex, k1);
        int i2 = GetTriPacked(cubeIndex, k2);
        
        if (i0 == -1 || i1 == -1 || i2 == -1) break;

        /*
        float3 wp0 = v[i0];
        float3 wp1 = v[i1];
        float3 wp2 = v[i2];
        */
        
        
        /*
        int2 ec0 = edgeToCorners[i0];
        int2 ec1 = edgeToCorners[i1];
        int2 ec2 = edgeToCorners[i2];

        float3 wp0 = EdgeLerp(p[ec0.x], p[ec0.y], d[ec0.x], d[ec0.y], gIsoLevel);
        float3 wp1 = EdgeLerp(p[ec1.x], p[ec1.y], d[ec1.x], d[ec1.y], gIsoLevel);
        float3 wp2 = EdgeLerp(p[ec2.x], p[ec2.y], d[ec2.x], d[ec2.y], gIsoLevel);
        */
        
        
        if (!(edges & (1 << i0)))
        {
            int2 ec = edgeToCorners[i0];
            v[i0] = EdgeLerp(p[ec.x], p[ec.y], d[ec.x], d[ec.y], gIsoLevel);
        }
        if (!(edges & (1 << i1)))
        {
            int2 ec = edgeToCorners[i1];
            v[i1] = EdgeLerp(p[ec.x], p[ec.y], d[ec.x], d[ec.y], gIsoLevel);
        }
        if (!(edges & (1 << i2)))
        {
            int2 ec = edgeToCorners[i2];
            v[i2] = EdgeLerp(p[ec.x], p[ec.y], d[ec.x], d[ec.y], gIsoLevel);
        }
        

        float3 wp0 = v[i0];
        float3 wp1 = v[i1];
        float3 wp2 = v[i2];

        float3 n0 = CalcGradient(wp0);
        float3 n1 = CalcGradient(wp1);
        float3 n2 = CalcGradient(wp2);
        
        //float3 fn = normalize(cross(wp1 - wp0, wp2 - wp0));
        float3 fn = cross(wp1 - wp0, wp2 - wp0);
        float3 an = normalize(n0 + n1 + n2);
        
        if (dot(fn, an) < 0.0f)
        {
            float3 tmpP = wp1;
            wp1 = wp2;
            wp2 = tmpP;
            
            float3 tmpN = n1;
            n1 = n2;
            n2 = tmpN;
        }
        
        /*
        float3 fn = normalize(cross(wp1 - wp0, wp2 - wp0));
        float3 an = normalize(n0 + n1 + n2);
        if (dot(fn, an) < 0.0)
        {
            float3 tp = wp1;
            wp1 = wp2;
            wp2 = tp;
            float3 tn = n1;
            n1 = n2;
            n2 = tn;
        }*/
        
        /*
        float3 e1 = wp1 - wp0;
        float3 e2 = wp2 - wp0;
        float3 faceNormal = cross(e1, e2);
        
        float3 avgGrad = normalize(n0 + n1 + n2);
        if (dot(faceNormal, avgGrad) < 0.0)
        {
            float3 temp = wp1;
            wp1 = wp2;
            wp2 = temp;
            float3 tempN = n1;
            n1 = n2;
            n2 = tempN;
        }*/

        VertexOut A = (VertexOut) 0;
        VertexOut B = (VertexOut) 0;
        VertexOut C = (VertexOut) 0;

        A.Pos = wp0;
        A.Normal = n0;
        A.Tangent = TangentFromNormal(n0);
        A.TexC = 0;
        B.Pos = wp1;
        B.Normal = n1;
        B.Tangent = TangentFromNormal(n1);
        B.TexC = 0;
        C.Pos = wp2;
        C.Normal = n2;
        C.Tangent = TangentFromNormal(n2);
        C.TexC = 0;

        //gVertices.Append(A);
        //gVertices.Append(B);
        //gVertices.Append(C);
        uint baseVert;
        gVertCount.InterlockedAdd(0, 3, baseVert);
        
        
        gVertices[baseVert + 0] = A;
        gVertices[baseVert + 1] = B;
        gVertices[baseVert + 2] = C;
    }
}