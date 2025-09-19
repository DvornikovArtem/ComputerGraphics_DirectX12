// TerrainImporter.h

#ifndef TERRAINIMPORTER_H
#define TERRAINIMPORTER_H


#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <DirectXCollision.h>

// Extern libraries
#include <DirectXTex/DirectXTex.h>

// From solution
#include <Engine/Core/DxException.h>


// A structure that stores information about each quadtree tile
struct TerrainTileInfo {

    // LOD for this tile at the current moment in time
    uint16_t lod;

    // The indices of this tile in the grid of the given quadtree LOD level
    // E.g. at the second level there are 4 tiles.
    // Their indices are (0, 0), (0, 1), (1, 0), (1, 1)
    uint32_t ix, iy;

    // Relative paths to the textures of this tile
    std::wstring diffusePath, normalPath, heightPath;

    // A 3D box (AABB) bounding the tile in world space (for frustum culling and intersection checks with other objects)
    DirectX::BoundingBox bounds;

    // The minimum and maximum height in this tile (from the heightMap, i.e. (id est) before applying heightScale)
    float minH, maxH;
};


// A structure that stores information about the entire terrain
struct TerrainMeta {

    // The terrain dimensions in world space along the X and Z axes (width and depth)
    float worldSizeX, worldSizeZ;

    // Height scaling factor (height map values are multiplied by it)
    float heightScale = 1.0f;

    // The tile size in pixels at the highest level of the tree (i.e. the size of the smallest tile in the entire tree)
    uint32_t baseTilePixels = 0;

    // The size of a tile in world coordinates
    float baseTileWorldSize = 1.f;

    // The total number of LOD levels in the QuadTree.
    // E.g. quadLevels = 3  ->  the levels will be LOD = 0, 1, 2.
    uint32_t quadLevels = 1;

    // An array of all tiles across all LODs
    std::vector<TerrainTileInfo> tiles;

    // The folder containing all generated tiles (diffuse/normal/height).
    // This folder is located relative to the diffuse texture of the entire terrain at <diffuseMap>/Tiles/
    std::wstring tilesRootDir;

    // Render the terrain as a wireframe
    bool enableWireFrame = false;
};



class TerrainImporter
{
public:
    TerrainImporter() = default;

    bool BuildTilesFromSource(
        const std::wstring& diffuse,
        const std::wstring& normal,
        const std::wstring& height,
        uint32_t quadLevels,
        TerrainMeta& outMeta
    );

private:
    static std::filesystem::path MakeTilesDir(const std::wstring& diffusePath);
};



#endif // TERRAINIMPORTER_H