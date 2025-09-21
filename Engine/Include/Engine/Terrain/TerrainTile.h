// TerrainTile.h

#ifndef TERRAINTILE_H
#define TERRAINTILE_H

#include <string>
#include <cstdint>
#include <DirectXCollision.h>



struct RenderItem;



struct TerrainTileTextures
{
    std::wstring diffusePath;
    std::wstring normalPath;
    std::wstring heightPath;

    uint8_t diffuseSelectedMip = 0;
    uint8_t normalSelectedMip = 0;
    uint8_t heightSelectedMip = 0;

    uint8_t diffuseMipCount = 0;
    uint8_t normalMipCount = 0;
    uint8_t heightMipCount = 0;
};



struct TerrainTileWorldRect
{
    // Left-bottom tile corner in the world
    float x0 = 0.f, z0 = 0.f;
    float sizeX = 0.f, sizeZ = 0.f;
};



struct TerrainTile
{
    // Level in quadTree
    uint16_t lod = 0;

    // Column in level
    uint32_t ix = 0;

    // Row in level
    uint32_t iy = 0;

    float minH = 0.f;
    float maxH = 0.f;

    float deltaH() const { return maxH - minH; }

    DirectX::BoundingBox bounds{};

    TerrainTileWorldRect worldRect{};

    TerrainTileTextures textures{};

    RenderItem* renderItem = nullptr;


    // Obtain a single numeric key, convenient to use as a key in unordered_map <uint64_t, ...>, std::map, resource cache, descriptor pool, etc.
    uint64_t Key() const { return (uint64_t(lod) << 48) | (uint64_t(ix) << 24) | uint64_t(iy); }
};

#endif // TERRAINTILE_H