// TerrainRenderer.h


#ifndef TERRAINRENDERER_H
#define TERRAINRENDERER_H



#include <Engine/Terrain/TerrainImporter.h>
#include <Engine/Terrain/TerrainQuadTree.h>
#include <Engine/Scene/Camera.h>
#include <unordered_set>


struct TerrainRendererDesc
{
	std::wstring terrainName = L"Undefined";

	std::wstring pathToDiffuseMap;
	std::wstring pathToHeightMap;
	std::wstring pathToNormalMap;

	float minHeight = -(std::numeric_limits<float>::infinity)();
	float maxHeight = (std::numeric_limits<float>::infinity)();

	float heightMapScale = 1.0f;

	uint8_t quadTreeLevels = 4;

	bool enableWireFrame = false;
};


class TerrainRenderer
{
public:
	TerrainRenderer() = default;

	~TerrainRenderer()
	{
		if (terrainImporter) delete terrainImporter;
	};

	void Initialize(const TerrainRendererDesc& terrainRendererDesc);

	//void BuildGeometry(Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice, Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList, std::unordered_map<std::string, MeshGeometry*> mGeometries);

	const TerrainQuadTree& Quad() const { return m_quad; }
	const TerrainMeta& Meta() const { return m_meta; }

	template<typename Function>
	void ForEachTile(Function&& function) const {
		m_quad.ForEachNode([&](const TerrainNode& n) { function(n.tile); });
	}

	void SelectLOD(const Camera& cam, std::vector<RenderItem*>& outVisible, float lodFactor = 0.02f) const;

private:
	TerrainImporter* terrainImporter = nullptr;
	TerrainMeta      m_meta{};
	TerrainQuadTree  m_quad{};
};

#endif // TERRAINRENDERER_H