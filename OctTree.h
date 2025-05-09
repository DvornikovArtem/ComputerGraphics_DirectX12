#pragma once

#ifndef OCTTREE_H
#define OCTTREE_H

#include <array>
#include <vector>
#include <string>

#include <DirectXCollision.h>
#include "old/DebugRenderSysImpl.h"
#include "RenderItem.h"
#include <set>


using namespace DirectX;


struct OctTreeNode {
    BoundingBox bounds;
    std::array<OctTreeNode*, 8> children;
    std::vector<RenderItem*> OverlappedItems;
    std::vector<LightObject*> OverlappedLightObjects;
    bool isLeaf = false;
    int level = 0;
};


struct OctTreeDesc {
    std::vector<RenderItem*>* ritems = nullptr;
    std::vector<LightObject*>* lightItems = nullptr;
    size_t numDivisions = 3;
    bool autoFitBox = true;
    XMFLOAT3 center = { 0.0f, 0.0f, 0.0f };
    float cubeSize = 1000.0f;
};


class OctTree {
private:
    OctTreeNode* root;
    size_t numDivisions;
    std::vector<std::vector<OctTreeNode*>> levels;

public:
    OctTree(const OctTreeDesc& octTreeDesc)
    {
        this->numDivisions = octTreeDesc.numDivisions;

        root = new OctTreeNode;
        root->level = 0;

        DirectX::BoundingBox sceneBBox;

        if (octTreeDesc.autoFitBox) {
            // Calculate Common Bbox As The Parallelepiped That Covers All RenderItems On The Scene ===============================================================
            sceneBBox = (*octTreeDesc.ritems)[0]->bounds;
            std::set<RenderLayer> includedLayers = { RenderLayer::Opaque, RenderLayer::Transparent };
            for (auto& ritem : (*octTreeDesc.ritems)) if (includedLayers.count(ritem->renderLayer)) DirectX::BoundingBox::CreateMerged(sceneBBox, sceneBBox, ritem->bounds);
            // ====================================================================================================================================================


            // Return The Size Of The Total Box To The min Cube That Can Accommodate The Entire Scene =============================================================
            XMFLOAT3 center = sceneBBox.Center;
            XMFLOAT3 extents = sceneBBox.Extents;

            float maxExtent = extents.x > extents.y ? extents.x : extents.y;
            maxExtent = extents.z > maxExtent ? extents.z : maxExtent;
            BoundingBox cubeSceneBbox(center, XMFLOAT3(maxExtent, maxExtent, maxExtent));
            // ====================================================================================================================================================

            root->bounds = cubeSceneBbox;
        }
        else {
            XMFLOAT3 extents(octTreeDesc.cubeSize / 2.0f, octTreeDesc.cubeSize / 2.0f, octTreeDesc.cubeSize / 2.0f);
            root->bounds = BoundingBox(octTreeDesc.center, extents);
        }

        levels.resize(numDivisions);

        BuildTree(root, 0, numDivisions);

        for (auto& ri : (*octTreeDesc.ritems)) {
            if (ri->renderLayer == RenderLayer::Sky) continue;
            std::vector<OctTreeNode*> intersectingLeaves;
            FindIntersectingLeaves(ri->bounds, intersectingLeaves);

            for (auto& leaf : intersectingLeaves) {
                leaf->OverlappedItems.push_back(ri);
            }

            ri->occupiedLeaves = std::move(intersectingLeaves);
        }

        for (auto& li : (*octTreeDesc.lightItems)) {
            if (li->LightType == LightType::Directional) continue;
            std::vector<OctTreeNode*> intersectingLeaves;
            FindIntersectingLeaves(li->bounds, intersectingLeaves);

            for (auto& leaf : intersectingLeaves) {
                leaf->OverlappedLightObjects.push_back(li);
            }

            li->occupiedLeaves = std::move(intersectingLeaves);
        }
    }

    ~OctTree() { DeleteTree(root); }

    OctTreeNode* getRoot() { return root; }

    size_t getNumDivisions() const { return numDivisions; }

    // Highest Level = 0, Lowest Level = (numDivisions - 1)
    std::vector<OctTreeNode*> GetAllNodesAtLevel(int level) { return levels[level]; }

    void Draw(gfw::DebugRenderSysImpl* debugDrawer)
    {
        for (auto bbox : GetAllNodesAtLevel(this->numDivisions - 1)) if (bbox->OverlappedLightObjects.size() > 0) debugDrawer->DrawBoundingBox(bbox->bounds);
        //for (auto bbox : GetAllNodesAtLevel(this->numDivisions - 1)) debugDrawer->DrawBoundingBox(bbox->bounds);
    }

    void UpdateRenderItemTreeLocation(RenderItem* ri)
    {
        for (auto& leaf : ri->occupiedLeaves) {
            auto& vec = leaf->OverlappedItems;
            vec.erase(std::remove(vec.begin(), vec.end(), ri), vec.end());
        }

        std::vector<OctTreeNode*> newLeaves;
        FindIntersectingLeaves(ri->bounds, newLeaves);
        for (auto& leaf : newLeaves) {
            leaf->OverlappedItems.push_back(ri);
        }

        ri->occupiedLeaves = std::move(newLeaves);
    }

    void UpdateLightItemTreeLocation(LightObject* li)
    {
        for (auto& leaf : li->occupiedLeaves) {
            auto& vec = leaf->OverlappedLightObjects;
            vec.erase(std::remove(vec.begin(), vec.end(), li), vec.end());
        }

        std::vector<OctTreeNode*> newLeaves;
        FindIntersectingLeaves(li->bounds, newLeaves);
        for (auto& leaf : newLeaves) {
            leaf->OverlappedLightObjects.push_back(li);
        }

        li->occupiedLeaves = std::move(newLeaves);
    }


private:

    void BuildTree(OctTreeNode* node, int currentLevel, size_t maxLevel) {
        if (currentLevel >= maxLevel) return;

        levels[currentLevel].push_back(node);

        const XMFLOAT3& parentCenter = node->bounds.Center;
        const XMFLOAT3 parentExtents = node->bounds.Extents;
        XMFLOAT3 childExtents = { parentExtents.x / 2.0f, parentExtents.y / 2.0f, parentExtents.z / 2.0f };

        for (int i = 0; i < 8; ++i) {
            XMFLOAT3 childCenter = parentCenter;
            childCenter.x += (i & 1) ? childExtents.x : -childExtents.x;
            childCenter.y += (i & 2) ? childExtents.y : -childExtents.y;
            childCenter.z += (i & 4) ? childExtents.z : -childExtents.z;

            node->children[i] = new OctTreeNode();
            node->children[i]->bounds = BoundingBox(childCenter, childExtents);
            node->children[i]->isLeaf = currentLevel + 2 == maxLevel;
            node->children[i]->level = currentLevel + 1;

            BuildTree(node->children[i], currentLevel + 1, maxLevel);
        }
    }

    void FindIntersectingLeaves(const BoundingBox& itemBounds, std::vector<OctTreeNode*>& result)
    {
        for (auto& leaf : levels[numDivisions - 1]) {
            if (leaf->bounds.Intersects(itemBounds)) {
                result.push_back(leaf);
            }
        }
    }

    void DeleteTree(OctTreeNode* node)
    {
        if (!node) return;

        for (OctTreeNode* child : node->children) DeleteTree(child);

        delete node;
    }
};

#endif // OCTTREE_H
