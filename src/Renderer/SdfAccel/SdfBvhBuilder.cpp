#include "SdfBvhBuilder.h"

#include "SdfAccelBoundsCore.h"

#include <algorithm>
#include <vector>

namespace {

struct BvhBuildEntry
{
    int begin = 0;
    int end = 0;
};

SdfFloat3 boundsCenter(const SdfAccelObjectGpu& object)
{
    return sdfScale3(sdfAdd3(object.boundsMin, object.boundsMax), 0.5f);
}

int longestAxis(SdfFloat3 extent)
{
    if (extent.x >= extent.y && extent.x >= extent.z) {
        return 0;
    }
    if (extent.y >= extent.z) {
        return 1;
    }
    return 2;
}

SdfFloat3 boundsExtent(const SdfAccelObjectGpu& object)
{
    return sdfSub3(object.boundsMax, object.boundsMin);
}

SdfBvhNode makeLeafNode(const SdfAccelObjectGpu& object, uint32_t objectIndex)
{
    SdfBvhNode node{};
    node.boundsMin = object.boundsMin;
    node.boundsMax = object.boundsMax;
    node.objectIndex = objectIndex;
    node.flags = SdfBvhFlagLeaf;
    return node;
}

int buildRecursive(
    const std::vector<SdfAccelObjectGpu>& objects,
    std::vector<int>& indices,
    int begin,
    int end,
    std::vector<SdfBvhNode>& nodes)
{
    SdfFloat3 boundsMin = objects[static_cast<size_t>(indices[static_cast<size_t>(begin)])].boundsMin;
    SdfFloat3 boundsMax = objects[static_cast<size_t>(indices[static_cast<size_t>(begin)])].boundsMax;

    for (int i = begin + 1; i < end; ++i) {
        const SdfAccelObjectGpu& object = objects[static_cast<size_t>(indices[static_cast<size_t>(i)])];
        boundsMin.x = sdfMin2(boundsMin.x, object.boundsMin.x);
        boundsMin.y = sdfMin2(boundsMin.y, object.boundsMin.y);
        boundsMin.z = sdfMin2(boundsMin.z, object.boundsMin.z);
        boundsMax.x = sdfMax2(boundsMax.x, object.boundsMax.x);
        boundsMax.y = sdfMax2(boundsMax.y, object.boundsMax.y);
        boundsMax.z = sdfMax2(boundsMax.z, object.boundsMax.z);
    }

    const int nodeIndex = static_cast<int>(nodes.size());
    SdfBvhNode node{};
    node.boundsMin = boundsMin;
    node.boundsMax = boundsMax;
    nodes.push_back(node);

    const int count = end - begin;
    if (count == 1) {
        nodes[static_cast<size_t>(nodeIndex)] =
            makeLeafNode(objects[static_cast<size_t>(indices[static_cast<size_t>(begin)])], static_cast<uint32_t>(indices[static_cast<size_t>(begin)]));
        nodes[static_cast<size_t>(nodeIndex)].boundsMin = boundsMin;
        nodes[static_cast<size_t>(nodeIndex)].boundsMax = boundsMax;
        return nodeIndex;
    }

    const SdfFloat3 extent = sdfSub3(boundsMax, boundsMin);
    const int axis = longestAxis(extent);
    const int mid = begin + count / 2;

    std::nth_element(
        indices.begin() + begin,
        indices.begin() + mid,
        indices.begin() + end,
        [&objects, axis](int lhs, int rhs) {
            const SdfFloat3 cL = boundsCenter(objects[static_cast<size_t>(lhs)]);
            const SdfFloat3 cR = boundsCenter(objects[static_cast<size_t>(rhs)]);
            if (axis == 0) {
                return cL.x < cR.x;
            }
            if (axis == 1) {
                return cL.y < cR.y;
            }
            return cL.z < cR.z;
        });

    const int leftIndex = buildRecursive(objects, indices, begin, mid, nodes);
    const int rightIndex = buildRecursive(objects, indices, mid, end, nodes);

    nodes[static_cast<size_t>(nodeIndex)].leftIndex = static_cast<uint32_t>(leftIndex);
    nodes[static_cast<size_t>(nodeIndex)].rightIndex = static_cast<uint32_t>(rightIndex);
    nodes[static_cast<size_t>(nodeIndex)].flags = 0;
    return nodeIndex;
}

} // namespace

bool sdfAccelBuildBvh(
    const std::vector<SdfAccelObjectGpu>& objects,
    std::vector<SdfBvhNode>& outNodes,
    uint32_t& outRootIndex)
{
    outNodes.clear();
    outRootIndex = 0;

    if (objects.empty()) {
        return false;
    }

    std::vector<int> indices(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) {
        indices[i] = static_cast<int>(i);
    }

    const int rootIndex = buildRecursive(objects, indices, 0, static_cast<int>(objects.size()), outNodes);
    if (rootIndex < 0 || outNodes.empty()) {
        return false;
    }

    outRootIndex = static_cast<uint32_t>(rootIndex);
    return true;
}
