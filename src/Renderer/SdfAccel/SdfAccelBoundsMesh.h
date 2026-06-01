#pragma once

#include "SdfAccelScene.h"
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
    std::vector<SdfAccelBoundsLineVertex> octreeLines;
};

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

        for (uint32_t nodeIndex = nodeStart; nodeIndex < nodeEnd; ++nodeIndex) {
            const SdfOctreeNode& node = octreeNodes[nodeIndex];
            const SdfFloat3 worldCenter = sdfAdd3(node.center, object.center);
            const SdfFloat3 boundsMin = sdfSub3(worldCenter, node.halfExtent);
            const SdfFloat3 boundsMax = sdfAdd3(worldCenter, node.halfExtent);
            sdfAccelAppendAabbWireframe(
                mesh.octreeLines,
                boundsMin,
                boundsMax,
                octreeR,
                octreeG,
                octreeB);
        }
    }

    return mesh;
}
