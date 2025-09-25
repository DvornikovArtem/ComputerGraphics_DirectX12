// TerrainRenderer.cpp

#include <Engine/Terrain/TerrainRenderer.h>
#include <Engine/Math/GeometryGenerator.h>
#include <Engine/RHI/DX12/FrameResource.h>
#include <Engine/Render/Descriptors.h>
#include <Engine/Render/RenderItem.h>

using namespace DirectX;

void TerrainRenderer::Initialize(const TerrainRendererDesc& terrainRendererDesc)
{
    // quadTreeLevels should be > 0
    if (terrainRendererDesc.quadTreeLevels < 1) ThrowError(L"TerrainRenderer::Initialize", L"The number of quadtree levels must be an integer > 0");

    // minHeight < maxHeight
    if (terrainRendererDesc.minHeight >= terrainRendererDesc.maxHeight) ThrowError(L"TerrainRenderer::Initialize", L"minHeight must be < maxHeight");


    terrainImporter = new TerrainImporter();
    
    TerrainMeta meta{};
    meta.heightScale = terrainRendererDesc.heightMapScale;
    meta.enableWireFrame = terrainRendererDesc.enableWireFrame;


    const bool ok = terrainImporter->BuildTilesFromSource(
        terrainRendererDesc.pathToDiffuseMap,
        terrainRendererDesc.pathToNormalMap,
        terrainRendererDesc.pathToHeightMap,
        terrainRendererDesc.quadTreeLevels,
        meta
    );

    if (!ok) ThrowError(L"TerrainRenderer::Initialize", L"BuildTilesFromSource failed");


    // Instead of copying the information about the entire terrain, we simply move the pointer to this information into another variable
    m_meta = std::move(meta);
    m_quad.BuildFromMeta(m_meta);
}


void TerrainRenderer::SelectLOD(const Camera& cam, std::vector<RenderItem*>& outVisible, float lodFactor) const
{
    outVisible.clear();

    const float splitFrac = (lodFactor > 0.f ? lodFactor : 0.02f);
    const float mergeFrac = splitFrac * 0.7f;

    BoundingFrustum fr;
    XMMATRIX P = cam.GetProj();
    BoundingFrustum::CreateFromMatrix(fr, P);
    XMMATRIX invV = XMMatrixInverse(nullptr, cam.GetView());
    fr.Transform(fr, invV);

    auto TanHalfFovY_FromProj = [](const XMMATRIX& M) -> float {
        XMFLOAT4X4 Pf; XMStoreFloat4x4(&Pf, M);
        const float m22 = Pf._22;
        return (m22 != 0.f) ? (1.0f / m22) : 1.0f;
        };
    const float tanHalfFovY = TanHalfFovY_FromProj(cam.GetProj());

    auto DistSqPointAABB = [](const XMFLOAT3& p, const BoundingBox& b) -> float {
        const float px = p.x, py = p.y, pz = p.z;
        const float minx = b.Center.x - b.Extents.x, maxx = b.Center.x + b.Extents.x;
        const float miny = b.Center.y - b.Extents.y, maxy = b.Center.y + b.Extents.y;
        const float minz = b.Center.z - b.Extents.z, maxz = b.Center.z + b.Extents.z;
        float dx = 0.f, dy = 0.f, dz = 0.f;
        if (px < minx) dx = (minx - px); else if (px > maxx) dx = (px - maxx);
        if (py < miny) dy = (miny - py); else if (py > maxy) dy = (py - maxy);
        if (pz < minz) dz = (minz - pz); else if (pz > maxz) dz = (pz - maxz);
        return dx * dx + dy * dy + dz * dz;
        };

    auto ChildOrderFrontToBack = [](const TerrainNode* n, const XMFLOAT3& camPos) -> std::array<int, 4> {
        struct Item { int idx; float d2; };
        std::array<Item, 4> tmp{};
        for (int i = 0; i < 4; ++i)
        {
            if (auto* c = n->children[i])
            {
                const float dx = c->tile.bounds.Center.x - camPos.x;
                const float dy = c->tile.bounds.Center.y - camPos.y;
                const float dz = c->tile.bounds.Center.z - camPos.z;
                tmp[i] = { i, dx * dx + dy * dy + dz * dz };
            }
            else
            {
                tmp[i] = { i, std::numeric_limits<float>::infinity() };
            }
        }
        std::sort(tmp.begin(), tmp.end(), [](const Item& a, const Item& b) { return a.d2 < b.d2; });
        return { tmp[0].idx, tmp[1].idx, tmp[2].idx, tmp[3].idx };
        };

    auto TileEdgeWorld = [&](const TerrainNode* node) -> float {
        const float edgeX = m_meta.worldSizeX / float(1u << node->tile.lod);
        const float edgeZ = m_meta.worldSizeZ / float(1u << node->tile.lod);
        return (edgeX > edgeZ) ? edgeX : edgeZ;
        };

    const XMFLOAT3 camPos = cam.GetPosition3f();

    std::function<void(const TerrainNode*)> recurse = [&](const TerrainNode* node)
        {
            if (!fr.Intersects(node->tile.bounds)) return;

            const float distSq = DistSqPointAABB(camPos, node->tile.bounds);
            float dist = std::sqrt((std::max)(distSq, 1e-12f));

            const float edge = TileEdgeWorld(node);
            const float screenFrac = edge / (2.0f * (std::max)(dist, 1e-6f) * (std::max)(tanHalfFovY, 1e-6f));

            bool wantChildren = false;
            if (node->lodSticky == 2) wantChildren = (screenFrac > mergeFrac);
            else wantChildren = (screenFrac > splitFrac);

            if (wantChildren && !node->IsLeaf())
            {
                const_cast<TerrainNode*>(node)->lodSticky = 2;

                auto order = ChildOrderFrontToBack(node, camPos);
                for (int k = 0; k < 4; ++k) if (auto* c = node->children[order[k]]) recurse(c);
            }
            else
            {
                const_cast<TerrainNode*>(node)->lodSticky = 1;

                if (node->terrainTileItem)
                {
                    node->terrainTileItem->currentLOD = 0;
                    outVisible.push_back(node->terrainTileItem);
                }
            }
        };

    if (m_quad.Levels() > 0) for (auto* n : m_quad.LevelNodes(0)) if (n) recurse(n);
}

//void TerrainRenderer::BuildGeometry(Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice, Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList, std::unordered_map<std::string, MeshGeometry*> mGeometries)
//{
//	
//}
