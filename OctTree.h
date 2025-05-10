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
    OctTreeNode* parent = nullptr;
    std::array<OctTreeNode*, 8> children;
    std::vector<RenderItem*> OverlappedRitems;
    std::vector<LightObject*> OverlappedLitems;
    bool isLeaf = false;
    int level = 0;
};


struct OctTreeDesc {
    std::vector<RenderItem*>* ritems = nullptr;
    std::vector<LightObject*>* litems = nullptr;
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
    std::vector<RenderItem*>* ritems = nullptr;
    std::vector<LightObject*>* litems = nullptr;

public:
    OctTree(const OctTreeDesc& octTreeDesc)
    {
        this->numDivisions = octTreeDesc.numDivisions;
        this->ritems = octTreeDesc.ritems;
        this->litems = octTreeDesc.litems;

        root = new OctTreeNode;
        root->level = 0;

        DirectX::BoundingBox sceneBBox;

        if (octTreeDesc.autoFitBox) {
            // Calculate Common Bbox As The Parallelepiped That Covers All RenderItems On The Scene ===============================================================
            std::set<RenderLayer> includedRitemsLayers = { RenderLayer::Opaque, RenderLayer::Transparent };
            for (auto& ritem : (*octTreeDesc.ritems)) if (includedRitemsLayers.count(ritem->renderLayer)) DirectX::BoundingBox::CreateMerged(sceneBBox, sceneBBox, ritem->bounds);
            std::set<LightType> includedLitemsLayers = { LightType::Pointlight, LightType::Spotlight };
            for (auto& litem : (*octTreeDesc.litems)) if (includedLitemsLayers.count(litem->LightType)) DirectX::BoundingBox::CreateMerged(sceneBBox, sceneBBox, litem->bounds);
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
    }

    ~OctTree() { DeleteTree(root); }

    OctTreeNode* getRoot() { return root; }

    size_t getNumDivisions() const { return numDivisions; }

    // Highest Level = 0, Lowest Level = (numDivisions - 1)
    std::vector<OctTreeNode*> GetAllNodesAtLevel(int level) { return levels[level]; }

    void Draw(gfw::DebugRenderSysImpl* debugDrawer)
    {
        //for (auto bbox : GetAllNodesAtLevel(this->numDivisions - 1)) if (bbox->OverlappedLightObjects.size() > 0) debugDrawer->DrawBoundingBox(bbox->bounds);
        for (auto bbox : GetAllNodesAtLevel(this->numDivisions - 1)) debugDrawer->DrawBoundingBox(bbox->bounds, Color(0.f, 0.f, 1.f, 1.f));
        //for (auto bbox : GetAllNodesAtLevel(0)) debugDrawer->DrawBoundingBox(bbox->bounds, Color(0.f, 0.f, 1.f, 1.f));
    }

    void checkToDeleteNode(OctTreeNode* node)
    {
        if (!node->OverlappedRitems.empty() || !node->OverlappedLitems.empty()) return;

        for (OctTreeNode* child : node->children) {
            if (child != nullptr) return;
        }

        OctTreeNode* parent = node->parent;
        if (parent) {
            for (int i = 0; i < 8; ++i) {
                if (parent->children[i] == node) {

                    delete node;
                    parent->children[i] = nullptr;

                    auto& vec = levels[parent->level + 1];
                    vec.erase(std::remove(vec.begin(), vec.end(), node), vec.end());

                    break;
                }
            }

            checkToDeleteNode(parent);
        }
    }

    void addNode(RenderItem* ri)
    {
        OctTreeNode* node = root;

        while (node->level < numDivisions - 1)
        {
            bool found = false;
            const XMFLOAT3& center = node->bounds.Center;
            const XMFLOAT3& extent = node->bounds.Extents;
            XMFLOAT3 childExtents = { extent.x / 2.0f, extent.y / 2.0f, extent.z / 2.0f };

            for (int i = 0; i < 8; ++i) {
                XMFLOAT3 childCenter = center;
                childCenter.x += (i & 1) ? childExtents.x : -childExtents.x;
                childCenter.y += (i & 2) ? childExtents.y : -childExtents.y;
                childCenter.z += (i & 4) ? childExtents.z : -childExtents.z;

                BoundingBox childBox(childCenter, childExtents);

                if (childBox.Intersects(ri->bounds)) {
                    if (!node->children[i]) {
                        OctTreeNode* child = new OctTreeNode();
                        child->parent = node;
                        child->bounds = childBox;
                        child->level = node->level + 1;
                        child->isLeaf = (child->level == numDivisions - 1);
                        node->children[i] = child;
                        levels[child->level].push_back(child);
                    }
                    node = node->children[i];
                    found = true;
                    break;
                }
            }

            if (!found) break;
        }

        node->OverlappedRitems.push_back(ri);
        ri->occupiedLeaves.push_back(node);
    }



    void UpdateRenderItemTreeLocation(RenderItem* ri)
    {
        if (ri->renderLayer == RenderLayer::Sky) return;


        for (auto& leaf : ri->occupiedLeaves) {
            auto& vec = leaf->OverlappedRitems;
            vec.erase(std::remove(vec.begin(), vec.end(), ri), vec.end());
        }

        std::vector<OctTreeNode*> newLeaves;
        FindIntersectingLeaves(ri->bounds, newLeaves);
        for (auto& leaf : newLeaves) {
            leaf->OverlappedRitems.push_back(ri);
        }

        if (newLeaves.empty()) {
            addNode(ri);
        }

        for (auto& leaf : ri->occupiedLeaves) {
            checkToDeleteNode(leaf);
        }

        ri->occupiedLeaves = std::move(newLeaves);
    }

    void UpdateLightItemTreeLocation(LightObject* li)
    {
        for (auto& leaf : li->occupiedLeaves) {
            auto& vec = leaf->OverlappedLitems;
            vec.erase(std::remove(vec.begin(), vec.end(), li), vec.end());
        }

        std::vector<OctTreeNode*> newLeaves;
        FindIntersectingLeaves(li->bounds, newLeaves);
        for (auto& leaf : newLeaves) {
            leaf->OverlappedLitems.push_back(li);
        }

        li->occupiedLeaves = std::move(newLeaves);
    }


private:

    void BuildTree(OctTreeNode* node, int currentLevel, size_t maxLevel) {

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
            node->children[i]->parent = node;
            node->children[i]->bounds = BoundingBox(childCenter, childExtents);
            node->children[i]->isLeaf = currentLevel + 2 == maxLevel;
            node->children[i]->level = currentLevel + 1;

            if (!doesNodeIntersectRitemsOrLitems(node->children[i])) {
                delete node->children[i];
                node->children[i] = nullptr;
                continue;
            }

            if (node->children[i]->level >= maxLevel) {
                delete node->children[i];
                node->children[i] = nullptr;
                continue;
            }

            BuildTree(node->children[i], currentLevel + 1, maxLevel);
        }
    }

    bool doesNodeIntersectRitemsOrLitems(OctTreeNode* node) {
        bool intersectAnyItem = false;

        for (auto& ri : *ritems) {
            if (ri->renderLayer == RenderLayer::Sky) continue;
            if (ri->bounds.Intersects(node->bounds)) {
                node->OverlappedRitems.push_back(ri);
                if (node->isLeaf) ri->occupiedLeaves.push_back(node);
                intersectAnyItem = true;
            }
        }

        for (auto& li : *litems) {
            if (li->LightType == LightType::Directional) continue;
            if (li->bounds.Intersects(node->bounds)) {
                node->OverlappedLitems.push_back(li);
                if (node->isLeaf) li->occupiedLeaves.push_back(node);
                intersectAnyItem = true;
            }
        }

        return intersectAnyItem;
    }

    void FindIntersectingLeaves(const BoundingBox& itemBounds, std::vector<OctTreeNode*>& result)
    {
        for (auto& leaf : levels[numDivisions - 1]) {
            if (leaf && leaf->bounds.Intersects(itemBounds)) {
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