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

//void TerrainRenderer::BuildGeometry(Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice, Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList, std::unordered_map<std::string, MeshGeometry*> mGeometries)
//{
//	
//}
