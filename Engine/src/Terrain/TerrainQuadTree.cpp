// TerrainQuadTree.cpp

#include <Engine/Terrain/TerrainQuadTree.h>

using namespace DirectX;



static TerrainTileWorldRect MakeWorldRect(const TerrainTileInfo& ti, const TerrainMeta& meta)
{
    // Based on how we populate the bounds in the importer, the tile width/height in the world is:
    // worldSizeX / 2^lod and worldSizeZ / 2^lod. The center and extents have already been calculated in the importer.
    const float worldTileX = meta.worldSizeX / float(1u << ti.lod);
    const float worldTileZ = meta.worldSizeZ / float(1u << ti.lod);
    TerrainTileWorldRect r;
    r.sizeX = worldTileX;
    r.sizeZ = worldTileZ;
    r.x0 = float(ti.ix) * worldTileX;
    r.z0 = float(ti.iy) * worldTileZ;
    return r;
}



TerrainNode* TerrainQuadTree::EmplaceNode(const TerrainTileInfo& ti, const TerrainMeta& meta)
{
    auto* node = new TerrainNode();

    node->tile.lod = ti.lod;
    node->tile.ix = ti.ix;
    node->tile.iy = ti.iy;
    node->tile.minH = ti.minH * meta.heightScale;
    node->tile.maxH = ti.maxH * meta.heightScale;
    node->tile.bounds = ti.bounds;
    node->tile.worldRect = MakeWorldRect(ti, meta);

    auto makeAbs = [&](const std::wstring& rel)->std::wstring {
        if (rel.empty()) return {};
        std::filesystem::path p(meta.tilesRootDir);
        p /= rel;
        return p.wstring();
        };

    node->tile.textures.diffusePath = makeAbs(ti.diffusePath);
    node->tile.textures.normalPath = makeAbs(ti.normalPath);
    node->tile.textures.heightPath = makeAbs(ti.heightPath);

    // Default mip 0
    node->tile.textures.diffuseSelectedMip = 0;
    node->tile.textures.normalSelectedMip = 0;
    node->tile.textures.heightSelectedMip = 0;

    // Place them into arrays
    if (m_nodesByLevel.size() <= ti.lod) m_nodesByLevel.resize(size_t(ti.lod) + 1);
    m_nodesByLevel[ti.lod].push_back(node);
    m_allNodes.push_back(node);

    m_map.insert({ TerrainNodeKey{ti.lod, ti.ix, ti.iy}, node });
    return node;
}



void TerrainQuadTree::LinkHierarchy()
{
    // For each node at level L > 0, assign parent = (L-1, ix/2, iy/2) and link the children in the parent
    for (size_t L = 1; L < m_nodesByLevel.size(); ++L)
    {
        for (TerrainNode* n : m_nodesByLevel[L])
        {
            uint32_t px = n->tile.ix >> 1;
            uint32_t py = n->tile.iy >> 1;
            auto it = m_map.find(TerrainNodeKey{ (uint16_t)(L - 1), px, py });
            if (it != m_map.end())
            {
                TerrainNode* p = it->second;
                n->parent = p;

                const uint32_t cx = n->tile.ix & 1u;
                const uint32_t cy = n->tile.iy & 1u;
                const uint32_t childIndex = (cy << 1) | cx; // (x,y)-> idx: (0,0)=0, (1,0)=1, (0,1)=2, (1,1)=3
                p->children[childIndex] = n;
            }
        }
    }
}



void TerrainQuadTree::BuildFromMeta(const TerrainMeta& meta)
{
    // Cleanup
    for (auto* n : m_allNodes) delete n;
    m_allNodes.clear();
    m_nodesByLevel.clear();
    m_map.clear();

    // We create nodes from meta.tiles (the importer has already filled them and sorted by LOD/ix/iy).
    // The order is not critical, but it's nice if the parent appears before the children — though this is not required
    for (const auto& ti : meta.tiles) EmplaceNode(ti, meta);

    // Link parents/children
    LinkHierarchy();
}

TerrainNode* TerrainQuadTree::Find(uint16_t lod, uint32_t ix, uint32_t iy) const
{
    auto it = m_map.find(TerrainNodeKey{ lod, ix, iy });
    return (it == m_map.end()) ? nullptr : it->second;
}