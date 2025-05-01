#pragma once

#ifndef OCTTREE_H
#define OCTTREE_H

#include <array>
#include <vector>
#include <string>

#include <DirectXCollision.h>
#include "old/DebugRenderSysImpl.h"
#include "RenderItem.h"


using namespace DirectX;


struct OctTreeNode {
    BoundingBox bounds;
    std::array<OctTreeNode*, 8> children;
    std::vector<RenderItem*> OverlappedItems;
    bool isLeaf = false;
    int level = 0;
};


class OctTree {
public:
    OctTree(const XMFLOAT3& center, float cubeSize, size_t numDivisions, const std::vector<RenderItem*>& ritems)
    {
        this->numDivisions = numDivisions;

        XMFLOAT3 extents(cubeSize / 2.0f, cubeSize / 2.0f, cubeSize / 2.0f);
        root = std::make_unique<OctTreeNode>();
        root->bounds = BoundingBox(center, extents);
        root->level = 0;

        levels.resize(numDivisions);

        BuildTree(root.get(), 0, numDivisions);

        for (auto& ri : ritems) {
            if (ri->renderLayer == RenderLayer::Sky) continue;
            std::vector<OctTreeNode*> intersectingLeaves;
            FindIntersectingLeaves(ri->bounds, intersectingLeaves);

            if (ri->drawableObject->Name == "Floor")
                OutputDebugString(L"Hello");

            for (auto& leaf : intersectingLeaves) {
                leaf->isLeaf = true;
                leaf->OverlappedItems.push_back(ri);
                leaf->isLeaf = true;
            }
        }
    }

    ~OctTree()
    {
        DeleteTree(root.get());
    }

    std::unique_ptr<OctTreeNode> root;

    std::vector<OctTreeNode*> GetNodesAtLevel(int level) {
        return levels[level];
    }

private:
    size_t numDivisions;
    std::vector<std::vector<OctTreeNode*>> levels;

    void BuildTree(OctTreeNode* node, int currentLevel, size_t maxLevel) {
        if (currentLevel >= maxLevel) return;

        levels[currentLevel].push_back(node);

        const XMFLOAT3& parentCenter = node->bounds.Center;
        const XMFLOAT3 parentExtents = node->bounds.Extents;
        XMFLOAT3 childExtents = {
            parentExtents.x / 2.0f,
            parentExtents.y / 2.0f,
            parentExtents.z / 2.0f
        };

        for (int i = 0; i < 8; ++i) {
            XMFLOAT3 childCenter = parentCenter;
            childCenter.x += (i & 1) ? childExtents.x : -childExtents.x;
            childCenter.y += (i & 2) ? childExtents.y : -childExtents.y;
            childCenter.z += (i & 4) ? childExtents.z : -childExtents.z;

            node->children[i] = new OctTreeNode();
            node->children[i]->bounds = BoundingBox(childCenter, childExtents);
            node->children[i]->isLeaf = (currentLevel + 1 == maxLevel);
            node->children[i]->level = currentLevel + 1;

            BuildTree(node->children[i], currentLevel + 1, maxLevel);
        }

        node->isLeaf = false;
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

        for (OctTreeNode* child : node->children)
        {
            if (child)
            {
                DeleteTree(child);
                delete child;
            }
        }
    }
};

#endif // OCTTREE_H