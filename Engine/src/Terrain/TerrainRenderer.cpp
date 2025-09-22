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
    BoundingFrustum fr;
    BoundingFrustum::CreateFromMatrix(fr, cam.GetProj());
    XMMATRIX invView = XMMatrixInverse(nullptr, cam.GetView());
    fr.Transform(fr, invView);

    std::function<void(const TerrainNode*)> recurse = [&](const TerrainNode* node)
        {
            if (!fr.Intersects(node->tile.bounds)) return;

            float dist = XMVectorGetX(XMVector3Length(XMLoadFloat3(&node->tile.bounds.Center) - XMLoadFloat3(&cam.GetPosition3f())));


            float tileSizeX = m_meta.worldSizeX / float(1u << node->tile.lod);
            float tileSizeZ = m_meta.worldSizeZ / float(1u << node->tile.lod);

            float threshold = tileSizeX * tileSizeZ * 0.002f;

            if (dist < threshold && !node->IsLeaf())
            {
                for (auto* c : node->children) if (c) recurse(c);
            }
            else
            {
                //if (node->terrainTileItem) outVisible.push_back(node->terrainTileItem);
                if (node->terrainTileItem)
                {
                    if (dist < 100.f) node->terrainTileItem->currentLOD = 0;
                    else if (dist < 200.f) node->terrainTileItem->currentLOD = (std::min)(node->terrainTileItem->numLODs - 1, (UINT)1);
                    else if (dist < 300.f) node->terrainTileItem->currentLOD = (std::min)(node->terrainTileItem->numLODs - 1, (UINT)2);
                    else if (dist < 400.f) node->terrainTileItem->currentLOD = (std::min)(node->terrainTileItem->numLODs - 1, (UINT)3);
                    else node->terrainTileItem->currentLOD = (std::min)(node->terrainTileItem->numLODs - 1, (UINT)4);

                    outVisible.push_back(node->terrainTileItem);
                }
            }
        };

    if (m_quad.Levels() > 0)
    {
        for (auto* n : m_quad.LevelNodes(0)) recurse(n);
    }
}

//void TerrainRenderer::BuildGeometry(Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice, Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList, std::unordered_map<std::string, MeshGeometry*> mGeometries)
//{
//	
//}
