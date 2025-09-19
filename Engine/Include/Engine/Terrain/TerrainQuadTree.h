// TerrainQuadTree.h

#ifndef TERRAINQUADTREE_H
#define TERRAINQUADTREE_H



#include <vector>
#include <unordered_map>
#include <functional>
#include <cassert>
#include <Engine/Terrain/TerrainTile.h>
#include <Engine/Terrain/TerrainImporter.h>



struct TerrainNode
{
    TerrainTile tile;

    TerrainNode* parent = nullptr;
    TerrainNode* children[4] = { nullptr, nullptr, nullptr, nullptr };

    bool IsLeaf() const { return !children[0] && !children[1] && !children[2] && !children[3]; }
};



struct TerrainNodeKey
{
    uint16_t lod; uint32_t ix; uint32_t iy;
    bool operator==(const TerrainNodeKey& o) const noexcept { return lod == o.lod && ix == o.ix && iy == o.iy; }
};



struct TerrainNodeKeyHash {
    size_t operator()(const TerrainNodeKey& k) const noexcept {
        size_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix(k.lod); mix(k.ix); mix(k.iy);
        return h;
    }
};



class TerrainQuadTree
{
public:
    TerrainQuadTree() = default;

    ~TerrainQuadTree()
    {
        for (auto* n : m_allNodes) delete n;
    }

    void BuildFromMeta(const TerrainMeta& meta);

    uint32_t Levels() const { return (uint32_t)m_nodesByLevel.size(); }

    const std::vector<TerrainNode*>& LevelNodes(uint32_t level) const { return m_nodesByLevel[level]; }

    template<typename Fn>
    void ForEachNode(Fn&& fn)
    {
        for (auto& lvl : m_nodesByLevel)
            for (auto* n : lvl) fn(*n);
    }

    template<typename Fn>
    void ForEachNode(Fn&& fn) const
    {
        for (auto const& lvl : m_nodesByLevel)
            for (auto* n : lvl) fn(*n);
    }

    TerrainNode* Find(uint16_t lod, uint32_t ix, uint32_t iy) const;

private:
    std::vector<std::vector<TerrainNode*>> m_nodesByLevel;
    std::vector<TerrainNode*> m_allNodes;
    std::unordered_map<TerrainNodeKey, TerrainNode*, TerrainNodeKeyHash> m_map;

    TerrainNode* EmplaceNode(const TerrainTileInfo& ti, const TerrainMeta& meta);
    void LinkHierarchy();
};



#endif // TERRAINQUADTREE_H