#pragma once

#ifndef RENDERITEM_H
#define RENDERITEM_H

#include "../Math/MathHelper.h"
#include "ShadowMap.h"

using namespace DirectX;

const int gNumFrameResources = 3;

enum class RenderLayer : int
{
    Opaque = 0,
    Mirrors,
    Reflected,
    Transparent,
    Shadow,
    Sky,
    Landscape,
    Count
};

struct RenderItem;
struct OctTreeNode;


struct DrawableObject
{
    DrawableObject() {}

    DrawableObject(std::string Name, std::string GeometryName, std::string MaterialName, RenderLayer renderLayer)
    {
        this->Name = Name;
        this->GeometryName = GeometryName;
        this->MaterialName = MaterialName;
        this->renderLayer = renderLayer;
    }
    DrawableObject(std::string Name, std::string GeometryName, std::string MaterialName, RenderLayer renderLayer, XMFLOAT3 WorldLocation, XMFLOAT3 WorldRotation, XMFLOAT3 Scale)
    {
        this->Name = Name;
        this->GeometryName = GeometryName;
        this->MaterialName = MaterialName;
        this->renderLayer = renderLayer;
        this->WorldLocation = WorldLocation;
        this->WorldRotation = WorldRotation;
        this->Scale = Scale;
    }

    ~DrawableObject() = default;

    std::string Name;
    std::string GeometryName;
    std::string MaterialName;
    RenderLayer renderLayer;

    RenderItem* renderItem;

    XMFLOAT3 WorldLocation = XMFLOAT3(0.f, 0.f, 0.f);
    XMFLOAT3 WorldRotation = XMFLOAT3(0.f, 0.f, 0.f);
    XMFLOAT3 Scale = XMFLOAT3(1.f, 1.f, 1.f);
    XMMATRIX TexTransform = XMMatrixIdentity();

    bool HasOutline = false;
    XMFLOAT3 OutlineColor = { 0.f, 0.f, 0.f };
};


// Lightweight structure stores parameters to draw a shape.
struct RenderItem
{
    RenderItem() = default;

    ~RenderItem() = default;

    BoundingBox bounds;

    std::vector<OctTreeNode*> occupiedLeaves;

    RenderLayer renderLayer;

    DrawableObject* drawableObject;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    // Dirty flag indicating the object data has changed and we need to update the constant buffer.
    // Because we have an object cbuffer for each FrameResource, we have to apply the
    // update to each FrameResource.  Thus, when we modify obect data we should set 
    // NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
    int NumFramesDirty = gNumFrameResources;

    // Index into GPU constant buffer corresponding to the ObjectCB for this render item.
    UINT ObjCBIndex = -1;

    Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;


    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;

    UINT numLODs = 1;
    UINT currentLOD = 0;

    bool IsInViewFrustum = false;

    DirectX::XMFLOAT4X4 PrevWorld = MathHelper::Identity4x4();
    bool InitFrame = true;
};



// ===== JUST EXAMPLE how we can do the 'key + hash + indexer' pattern for fast data access in std::unordered_map ======================
//
//struct TerrainTileItem : RenderItem
//{
//    uint16_t lod;
//    uint32_t ix, iy;
//    bool isRendered = false;
//};
//
//struct TerrainTileKey {
//    uint16_t lod;
//    uint32_t ix, iy;
//    bool operator==(const TerrainTileKey& o) const noexcept {
//        return lod == o.lod && ix == o.ix && iy == o.iy;
//    }
//};
//
//struct TerrainTileKeyHash {
//    size_t operator()(const TerrainTileKey& k) const noexcept {
//        size_t h = std::hash<int>{}(k.lod);
//        h ^= std::hash<int>{}(k.ix) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
//        h ^= std::hash<int>{}(k.iy) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
//        return h;
//    }
//};
//
//static TerrainTileItem* FindTerrainTile(uint16_t lod, uint32_t x, uint32_t y, std::vector<TerrainTileItem*>& objs, const std::unordered_map<TerrainTileKey, size_t, TerrainTileKeyHash>& idx)
//{
//    if (auto it = idx.find(TerrainTileKey{ lod, x, y }); it != idx.end()) return objs[it->second];
//    return nullptr;
//}
//
//idx.clear();
//idx.reserve(mAllTerrainRitems.size());
//for (size_t i = 0; i < mAllTerrainRitems.size(); i++) {
//    TerrainTileItem* terrainTileItem = mAllTerrainRitems[i];
//    idx.emplace(TerrainTileKey{ terrainTileItem->lod, terrainTileItem->ix, terrainTileItem->iy }, i);
//}
// 
//octTreeDesc.titemLOD0 = FindTerrainTile(0, 0, 0, mAllTerrainRitems, idx);
//
// ================================================================================================================================



struct LightObject
{
    LightObject() {}

    ~LightObject() {
        delete shadowMap;
        delete BlurredShadowMap;
    };

    BoundingBox bounds;

    std::vector<OctTreeNode*> occupiedLeaves;

    ShadowMap* shadowMap;
    ShadowMap* BlurredShadowMap;

    float Strength = 0.5f;
    float FalloffStart = 1.0f;                          // point/spot light only
    XMFLOAT3 WorldDirection = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    XMFLOAT3 WorldLocation = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;                            // spot light only
    XMFLOAT3 Color = { 1.f, 1.f, 1.f };
    LightType LightType = LightType::Pointlight;
    std::string Name = "";
    int LightCBIndex = 0; // auto generated value
    //bool NeedsUpdate = true;
    int NumFramesDirty = gNumFrameResources; // auto generated value
    MeshGeometry* Geo = nullptr; // auto generated value
    XMFLOAT4X4 World = MathHelper::Identity4x4();

    bool IsInViewFrustum = false;

    std::vector<RenderItem*> VisibleRitems;
    //frustum that defines the bounds for frustum culling
    BoundingFrustum LightFrustum;
};


#endif // RENDERITEM_H