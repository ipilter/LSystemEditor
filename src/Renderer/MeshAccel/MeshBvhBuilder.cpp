#include "MeshBvhBuilder.h"

#include "Geometry/MathCore.h"

#include <algorithm>
#include <vector>

namespace {

Vec3 triangleCentroid(const TriangleGpu& tri)
{
    return vecScale3(vecAdd3(vecAdd3(tri.v0, tri.v1), tri.v2), 1.0f / 3.0f);
}

Vec3 triangleBoundsMin(const TriangleGpu& tri)
{
    return vecMake3(
        vecMin3(tri.v0.x, tri.v1.x, tri.v2.x),
        vecMin3(tri.v0.y, tri.v1.y, tri.v2.y),
        vecMin3(tri.v0.z, tri.v1.z, tri.v2.z));
}

Vec3 triangleBoundsMax(const TriangleGpu& tri)
{
    return vecMake3(
        vecMax3(tri.v0.x, tri.v1.x, tri.v2.x),
        vecMax3(tri.v0.y, tri.v1.y, tri.v2.y),
        vecMax3(tri.v0.z, tri.v1.z, tri.v2.z));
}

int longestAxis(Vec3 extent)
{
    if (extent.x >= extent.y && extent.x >= extent.z) {
        return 0;
    }
    if (extent.y >= extent.z) {
        return 1;
    }
    return 2;
}

MeshBvhNode makeLeafNode(uint32_t triStart, uint32_t triCount, Vec3 boundsMin, Vec3 boundsMax)
{
    MeshBvhNode node{};
    node.boundsMin = boundsMin;
    node.boundsMax = boundsMax;
    node.triStart = triStart;
    node.triCount = triCount;
    return node;
}

int buildRecursive(
    const std::vector<TriangleGpu>& triangles,
    std::vector<int>& indices,
    int begin,
    int end,
    std::vector<MeshBvhNode>& nodes)
{
    Vec3 boundsMin = triangleBoundsMin(triangles[static_cast<size_t>(indices[static_cast<size_t>(begin)])]);
    Vec3 boundsMax = triangleBoundsMax(triangles[static_cast<size_t>(indices[static_cast<size_t>(begin)])]);

    for (int i = begin + 1; i < end; ++i) {
        const TriangleGpu& tri = triangles[static_cast<size_t>(indices[static_cast<size_t>(i)])];
        const Vec3 triMin = triangleBoundsMin(tri);
        const Vec3 triMax = triangleBoundsMax(tri);
        boundsMin.x = vecMin2(boundsMin.x, triMin.x);
        boundsMin.y = vecMin2(boundsMin.y, triMin.y);
        boundsMin.z = vecMin2(boundsMin.z, triMin.z);
        boundsMax.x = vecMax2(boundsMax.x, triMax.x);
        boundsMax.y = vecMax2(boundsMax.y, triMax.y);
        boundsMax.z = vecMax2(boundsMax.z, triMax.z);
    }

    const int nodeIndex = static_cast<int>(nodes.size());
    MeshBvhNode node{};
    node.boundsMin = boundsMin;
    node.boundsMax = boundsMax;
    nodes.push_back(node);

    const int count = end - begin;
    if (count == 1) {
        nodes[static_cast<size_t>(nodeIndex)] =
            makeLeafNode(static_cast<uint32_t>(indices[static_cast<size_t>(begin)]), 1u, boundsMin, boundsMax);
        return nodeIndex;
    }

    const Vec3 extent = vecSub3(boundsMax, boundsMin);
    const int axis = longestAxis(extent);
    const int mid = begin + count / 2;

    std::nth_element(
        indices.begin() + begin,
        indices.begin() + mid,
        indices.begin() + end,
        [&triangles, axis](int lhs, int rhs) {
            const Vec3 cL = triangleCentroid(triangles[static_cast<size_t>(lhs)]);
            const Vec3 cR = triangleCentroid(triangles[static_cast<size_t>(rhs)]);
            if (axis == 0) {
                return cL.x < cR.x;
            }
            if (axis == 1) {
                return cL.y < cR.y;
            }
            return cL.z < cR.z;
        });

    const int leftIndex = buildRecursive(triangles, indices, begin, mid, nodes);
    const int rightIndex = buildRecursive(triangles, indices, mid, end, nodes);

    nodes[static_cast<size_t>(nodeIndex)].leftIndex = static_cast<uint32_t>(leftIndex);
    nodes[static_cast<size_t>(nodeIndex)].rightIndex = static_cast<uint32_t>(rightIndex);
    nodes[static_cast<size_t>(nodeIndex)].triCount = 0;
    return nodeIndex;
}

} // namespace

bool meshAccelBuildBvh(
    const std::vector<TriangleGpu>& triangles,
    std::vector<MeshBvhNode>& outNodes,
    uint32_t& outRootIndex)
{
    outNodes.clear();
    outRootIndex = 0;

    if (triangles.empty()) {
        return false;
    }

    std::vector<int> indices(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) {
        indices[i] = static_cast<int>(i);
    }

    const int rootIndex = buildRecursive(triangles, indices, 0, static_cast<int>(triangles.size()), outNodes);
    if (rootIndex < 0 || outNodes.empty()) {
        return false;
    }

    outRootIndex = static_cast<uint32_t>(rootIndex);
    return true;
}
