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
    std::vector<SdfAccelBoundsLineVertex> bvhLines;
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

inline SdfAccelBoundsMesh sdfAccelBuildBoundsMesh(const SdfAccelScene& scene, const QColor& boundsColor)
{
    SdfAccelBoundsMesh mesh{};

    if (!scene.isBuilt()) {
        return mesh;
    }

    const float r = static_cast<float>(boundsColor.redF());
    const float g = static_cast<float>(boundsColor.greenF());
    const float b = static_cast<float>(boundsColor.blueF());

    for (const SdfBvhNode& node : scene.bvhNodesHost()) {
        sdfAccelAppendAabbWireframe(
            mesh.bvhLines,
            node.boundsMin,
            node.boundsMax,
            r,
            g,
            b);
    }

    return mesh;
}
