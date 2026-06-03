#include "SdfOctreeBuilder.h"

#include <vector>

namespace {

constexpr int kMaxOctreeBuildNodes = 26214400;

struct NodeIntervalData
{
    SdfAccelNodeInterval conservative{};
    float cornerMin = 0.0f;
    float cornerMax = 0.0f;
    float cornerD[8] = {};
};

NodeIntervalData computeNodeIntervalData(
    const SdfAccelField& field,
    SdfFloat3 localCenter,
    SdfFloat3 localHalfExtent)
{
    const SdfAccelAabb aabb = sdfAccelAabbFromCenterHalfExtent(localCenter, localHalfExtent);
    const float boundRadius = sdfAccelBoundRadius(localHalfExtent);
    const SdfFloat3 worldCenter = sdfAdd3(field.worldCenter, localCenter);
    const float dCenter = field.evalLocal(worldCenter);

    NodeIntervalData data{};
    float dCornerMin = dCenter;
    float dCornerMax = dCenter;
    for (int i = 0; i < 8; ++i) {
        const SdfFloat3 corner = sdfAccelAabbCorner(aabb, i);
        const SdfFloat3 worldCorner = sdfAdd3(field.worldCenter, corner);
        const float dCorner = field.evalLocal(worldCorner);
        data.cornerD[i] = dCorner;
        dCornerMin = sdfMin2(dCornerMin, dCorner);
        dCornerMax = sdfMax2(dCornerMax, dCorner);
    }

    data.conservative.dMin = sdfMin2(dCenter - boundRadius, dCornerMin);
    data.conservative.dMax = sdfMax2(dCenter + boundRadius, dCornerMax);
    data.cornerMin = dCornerMin;
    data.cornerMax = dCornerMax;
    return data;
}

uint8_t buildNodeFlags(float cornerMin, float cornerMax, float pruneEpsilon)
{
    uint8_t flags = SdfOctreeFlagLeaf;
    if (cornerMin <= 0.0f && cornerMax >= 0.0f) {
        flags |= SdfOctreeFlagStraddlesSurface;
    }
    if (cornerMax < -pruneEpsilon) {
        flags |= SdfOctreeFlagInsideSolid;
    }
    return flags;
}

bool shouldSubdivide(
    const SdfAccelNodeInterval& interval,
    SdfFloat3 halfExtent,
    int depth,
    const SdfAccelBuildParams& params,
    float minNodeSize)
{
    if (depth >= params.maxDepth) {
        return false;
    }

    if (sdfAccelMaxAxisHalfExtent(halfExtent) <= minNodeSize) {
        return false;
    }

    if (interval.dMax < -params.pruneEpsilon) {
        return false;
    }

    const bool straddles = interval.dMin <= 0.0f && interval.dMax >= 0.0f;
    return straddles;
}

int buildRecursive(
    const SdfAccelField& field,
    const SdfAccelBuildParams& params,
    float minNodeSize,
    SdfFloat3 localCenter,
    SdfFloat3 localHalfExtent,
    int depth,
    std::vector<SdfOctreeBuildNode>& buildNodes)
{
    const NodeIntervalData intervalData =
        computeNodeIntervalData(field, localCenter, localHalfExtent);
    const SdfAccelNodeInterval& interval = intervalData.conservative;
    if (interval.dMin > params.pruneEpsilon) {
        return -1;
    }

    if (static_cast<int>(buildNodes.size()) >= kMaxOctreeBuildNodes) {
        const int nodeIndex = static_cast<int>(buildNodes.size());
        SdfOctreeBuildNode node{};
        node.center = localCenter;
        node.halfExtent = localHalfExtent;
        node.dMin = interval.dMin;
        node.dMax = interval.dMax;
        for (int i = 0; i < 8; ++i) {
            node.cornerD[i] = intervalData.cornerD[i];
        }
        node.flags = buildNodeFlags(intervalData.cornerMin, intervalData.cornerMax, params.pruneEpsilon);
        buildNodes.push_back(node);
        return nodeIndex;
    }

    SdfOctreeBuildNode node{};
    node.center = localCenter;
    node.halfExtent = localHalfExtent;
    node.dMin = interval.dMin;
    node.dMax = interval.dMax;
    for (int i = 0; i < 8; ++i) {
        node.cornerD[i] = intervalData.cornerD[i];
    }
    node.flags = buildNodeFlags(intervalData.cornerMin, intervalData.cornerMax, params.pruneEpsilon);

    const int nodeIndex = static_cast<int>(buildNodes.size());
    buildNodes.push_back(node);

    if (!shouldSubdivide(interval, localHalfExtent, depth, params, minNodeSize)) {
        return nodeIndex;
    }

    const SdfFloat3 childHalfExtent = sdfAccelChildHalfExtent(localHalfExtent);
    uint8_t childMask = 0;
    for (int octant = 0; octant < 8; ++octant) {
        const SdfFloat3 childCenter = sdfAccelChildCenter(localCenter, localHalfExtent, octant);
        const int childIndex =
            buildRecursive(field, params, minNodeSize, childCenter, childHalfExtent, depth + 1, buildNodes);
        if (childIndex >= 0) {
            childMask |= static_cast<uint8_t>(1 << octant);
            buildNodes[nodeIndex].childBuildIndices[octant] = childIndex;
        }
    }

    buildNodes[nodeIndex].childMask = childMask;
    if (childMask == 0) {
        return nodeIndex;
    }

    buildNodes[nodeIndex].flags = static_cast<uint8_t>(buildNodes[nodeIndex].flags & ~SdfOctreeFlagLeaf);
    return nodeIndex;
}

void flattenContiguous(
    const std::vector<SdfOctreeBuildNode>& buildNodes,
    int buildIndex,
    std::vector<SdfOctreeNode>& outNodes)
{
    if (buildIndex < 0) {
        return;
    }

    const SdfOctreeBuildNode& buildNode = buildNodes[static_cast<size_t>(buildIndex)];
    const uint32_t nodeIndex = static_cast<uint32_t>(outNodes.size());

    SdfOctreeNode node{};
    node.center = buildNode.center;
    node.halfExtent = buildNode.halfExtent;
    node.dMin = buildNode.dMin;
    node.dMax = buildNode.dMax;
    node.childMaskAndFlags = sdfAccelPackChildMaskAndFlags(buildNode.childMask, buildNode.flags);
    node.firstChildIndex = 0;
    outNodes.push_back(node);

    if (buildNode.childMask == 0) {
        return;
    }

    outNodes[nodeIndex].firstChildIndex = static_cast<uint32_t>(outNodes.size());
    for (int octant = 0; octant < 8; ++octant) {
        if ((buildNode.childMask & (1 << octant)) != 0) {
            flattenContiguous(buildNodes, buildNode.childBuildIndices[octant], outNodes);
        }
    }
}

} // namespace

bool sdfAccelBuildOctreeForField(
    const SdfAccelField& field,
    const SdfAccelBuildParams& params,
    std::vector<SdfOctreeNode>& outNodes,
    uint32_t& outRootIndex)
{
    const SdfFloat3 localMin = sdfSub3(field.localBoundsMin, field.worldCenter);
    const SdfFloat3 localMax = sdfSub3(field.localBoundsMax, field.worldCenter);
    const SdfFloat3 paddedMin =
        sdfSub3(localMin, sdfMakeFloat3(params.boundsPadding, params.boundsPadding, params.boundsPadding));
    const SdfFloat3 paddedMax =
        sdfAdd3(localMax, sdfMakeFloat3(params.boundsPadding, params.boundsPadding, params.boundsPadding));

    const SdfFloat3 rootCenter = sdfScale3(sdfAdd3(paddedMin, paddedMax), 0.5f);
    const SdfFloat3 rootHalfExtent = sdfScale3(sdfSub3(paddedMax, paddedMin), 0.5f);
    const float minNodeSize = sdfAccelAutoMinNodeSize(rootHalfExtent, params.maxDepth);

    std::vector<SdfOctreeBuildNode> buildNodes;
    const int rootBuildIndex =
        buildRecursive(field, params, minNodeSize, rootCenter, rootHalfExtent, 0, buildNodes);
    if (rootBuildIndex < 0) {
        outNodes.clear();
        outRootIndex = 0;
        return false;
    }

    outNodes.clear();
    flattenContiguous(buildNodes, rootBuildIndex, outNodes);
    outRootIndex = 0;
    return !outNodes.empty();
}
