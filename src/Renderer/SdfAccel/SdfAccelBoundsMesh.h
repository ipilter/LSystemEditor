#pragma once

#include "SdfAccelScene.h"
#include "SdfAccelTraverseCore.h"
#include "SdfAccelTypes.h"
#include "SdfMathCore.h"

#include <QColor>
#include <vector>

struct SdfAccelBoundsLineVertex
{
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct SdfAccelBoundsMesh
{
    std::vector<SdfAccelBoundsLineVertex> aabbLines;
    std::vector<SdfAccelBoundsLineVertex> octreeFullLines;
    std::vector<SdfAccelBoundsLineVertex> octreeExteriorLines;
    std::vector<SdfAccelBoundsLineVertex> octreeLeavesLines;
};

inline bool sdfAccelOctreeNodeStraddles(const SdfOctreeNode& node)
{
    const uint8_t flags = sdfAccelFlagsFromPacked(node.childMaskAndFlags);
    return (flags & SdfOctreeFlagStraddlesSurface) != 0;
}

inline bool sdfAccelOctreeNodeIsSurfaceLeaf(const SdfOctreeNode& node)
{
    const uint8_t flags = sdfAccelFlagsFromPacked(node.childMaskAndFlags);
    return node.firstChildIndex == 0 && (flags & SdfOctreeFlagStraddlesSurface) != 0;
}

inline void sdfAccelAppendAabbWireframe(
    std::vector<SdfAccelBoundsLineVertex>& out,
    SdfFloat3 boundsMin,
    SdfFloat3 boundsMax,
    float r,
    float g,
    float b)
{
    const SdfFloat3 corners[8] = {
        sdfMakeFloat3(boundsMin.x, boundsMin.y, boundsMin.z),
        sdfMakeFloat3(boundsMax.x, boundsMin.y, boundsMin.z),
        sdfMakeFloat3(boundsMin.x, boundsMax.y, boundsMin.z),
        sdfMakeFloat3(boundsMax.x, boundsMax.y, boundsMin.z),
        sdfMakeFloat3(boundsMin.x, boundsMin.y, boundsMax.z),
        sdfMakeFloat3(boundsMax.x, boundsMin.y, boundsMax.z),
        sdfMakeFloat3(boundsMin.x, boundsMax.y, boundsMax.z),
        sdfMakeFloat3(boundsMax.x, boundsMax.y, boundsMax.z),
    };

    const int edges[12][2] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7},
        {0, 2}, {1, 3}, {4, 6}, {5, 7},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };

    for (const auto& edge : edges) {
        const SdfFloat3& a = corners[edge[0]];
        const SdfFloat3& c = corners[edge[1]];
        out.push_back({a.x, a.y, a.z, r, g, b});
        out.push_back({c.x, c.y, c.z, r, g, b});
    }
}

inline void sdfAccelAppendCoarsestExteriorWireframes(
    const std::vector<SdfOctreeNode>& octreeNodes,
    const SdfAccelObjectGpu& object,
    uint32_t nodeIndex,
    bool parentStraddles,
    float r,
    float g,
    float b,
    std::vector<SdfAccelBoundsLineVertex>& out)
{
    if (nodeIndex >= octreeNodes.size()) {
        return;
    }

    const SdfOctreeNode& node = octreeNodes[nodeIndex];
    const bool nodeStraddles = sdfAccelOctreeNodeStraddles(node);
    if (nodeStraddles && !parentStraddles) {
        const SdfFloat3 worldCenter = sdfAdd3(node.center, object.center);
        const SdfFloat3 boundsMin = sdfSub3(worldCenter, node.halfExtent);
        const SdfFloat3 boundsMax = sdfAdd3(worldCenter, node.halfExtent);
        sdfAccelAppendAabbWireframe(out, boundsMin, boundsMax, r, g, b);
    }

    if (node.firstChildIndex == 0) {
        return;
    }

    const uint8_t childMask = sdfAccelChildMaskFromPacked(node.childMaskAndFlags);
    for (int octant = 0; octant < 8; ++octant) {
        if ((childMask & (1 << octant)) == 0) {
            continue;
        }

        const uint32_t childIndex =
            node.firstChildIndex + static_cast<uint32_t>(sdfAccelPopCountMaskBelow(childMask, octant));
        sdfAccelAppendCoarsestExteriorWireframes(
            octreeNodes,
            object,
            childIndex,
            nodeStraddles,
            r,
            g,
            b,
            out);
    }
}

inline SdfAccelBoundsMesh sdfAccelBuildBoundsMesh(
    const SdfAccelScene& scene,
    const QColor& aabbColor,
    const QColor& octreeColor)
{
    SdfAccelBoundsMesh mesh{};

    if (!scene.isBuilt()) {
        return mesh;
    }

    const float aabbR = static_cast<float>(aabbColor.redF());
    const float aabbG = static_cast<float>(aabbColor.greenF());
    const float aabbB = static_cast<float>(aabbColor.blueF());
    const float octreeR = static_cast<float>(octreeColor.redF());
    const float octreeG = static_cast<float>(octreeColor.greenF());
    const float octreeB = static_cast<float>(octreeColor.blueF());

    for (const SdfAccelObjectGpu& object : scene.objectsHost()) {
        sdfAccelAppendAabbWireframe(
            mesh.aabbLines,
            object.boundsMin,
            object.boundsMax,
            aabbR,
            aabbG,
            aabbB);
    }

    const std::vector<SdfAccelObjectGpu>& objects = scene.objectsHost();
    const std::vector<SdfOctreeNode>& octreeNodes = scene.octreeNodesHost();
    for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        const SdfAccelObjectGpu& object = objects[objectIndex];
        const uint32_t nodeStart = object.octreeNodeOffset;
        const uint32_t nodeEnd = objectIndex + 1 < objects.size()
            ? objects[objectIndex + 1].octreeNodeOffset
            : static_cast<uint32_t>(octreeNodes.size());

        const uint32_t rootIndex = object.octreeNodeOffset + object.octreeRootIndex;
        sdfAccelAppendCoarsestExteriorWireframes(
            octreeNodes,
            object,
            rootIndex,
            false,
            octreeR,
            octreeG,
            octreeB,
            mesh.octreeExteriorLines);

        for (uint32_t nodeIndex = nodeStart; nodeIndex < nodeEnd; ++nodeIndex) {
            const SdfOctreeNode& node = octreeNodes[nodeIndex];
            const SdfFloat3 worldCenter = sdfAdd3(node.center, object.center);
            const SdfFloat3 boundsMin = sdfSub3(worldCenter, node.halfExtent);
            const SdfFloat3 boundsMax = sdfAdd3(worldCenter, node.halfExtent);

            sdfAccelAppendAabbWireframe(
                mesh.octreeFullLines,
                boundsMin,
                boundsMax,
                octreeR,
                octreeG,
                octreeB);

            if (sdfAccelOctreeNodeIsSurfaceLeaf(node)) {
                sdfAccelAppendAabbWireframe(
                    mesh.octreeLeavesLines,
                    boundsMin,
                    boundsMax,
                    octreeR,
                    octreeG,
                    octreeB);
            }
        }
    }

    return mesh;
}
