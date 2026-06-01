#pragma once

#include "SdfAccelBoundsCore.h"
#include "SdfAccelField.h"

SDF_ACCEL_CORE_FN float sdfAccelEvalPayloadWorld(
    SdfFloat3 worldP,
    SdfFloat3 objectCenter,
    const SdfAccelPayloadGpu* payload)
{
    if (payload == nullptr) {
        return 1.0e20f;
    }

    const SdfFloat3 localP = sdfSub3(worldP, objectCenter);
    switch (static_cast<SdfAccelPrimitiveType>(payload->type)) {
    case SdfAccelPrimitiveType::Sphere:
        return sdSphere(localP, payload->param0);
    case SdfAccelPrimitiveType::Cylinder:
        return sdCylinder(localP, payload->halfExtents);
    case SdfAccelPrimitiveType::CappedCone:
        return sdCappedCone(localP, payload->param0, payload->param1, payload->param2);
    default:
        return 1.0e20f;
    }
}

SDF_ACCEL_CORE_FN int sdfAccelPopCountMaskBelow(uint8_t childMask, int octant)
{
    int count = 0;
    for (int i = 0; i < octant; ++i) {
        if ((childMask & (1 << i)) != 0) {
            ++count;
        }
    }
    return count;
}

SDF_ACCEL_CORE_FN bool sdfAccelOctreeDescendToLeaf(
    SdfFloat3 localP,
    const SdfOctreeNode* nodes,
    uint32_t nodeIndex,
    uint32_t* outLeafIndex)
{
    if (nodes == nullptr || outLeafIndex == nullptr) {
        return false;
    }

    uint32_t current = nodeIndex;
    while (true) {
        const SdfOctreeNode& node = nodes[current];
        if (node.firstChildIndex == 0) {
            *outLeafIndex = current;
            return true;
        }

        const uint8_t childMask = sdfAccelChildMaskFromPacked(node.childMaskAndFlags);
        const int octant = sdfAccelOctantIndex(localP, node.center);
        if ((childMask & (1 << octant)) == 0) {
            return false;
        }

        const int childOffset = sdfAccelPopCountMaskBelow(childMask, octant);
        current = node.firstChildIndex + static_cast<uint32_t>(childOffset);
    }
}

SDF_ACCEL_CORE_FN float sdfAccelObjectSDF(
    SdfFloat3 worldP,
    const SdfAccelObjectGpu* object,
    const SdfOctreeNode* octreeNodes,
    const SdfAccelPayloadGpu* payloads)
{
    if (object == nullptr || octreeNodes == nullptr || payloads == nullptr) {
        return 1.0e20f;
    }

    const SdfFloat3 localP = sdfSub3(worldP, object->center);
    const uint32_t rootIndex = object->octreeNodeOffset + object->octreeRootIndex;

    uint32_t leafIndex = 0;
    if (!sdfAccelOctreeDescendToLeaf(localP, octreeNodes, rootIndex, &leafIndex)) {
        return sdfAccelEvalPayloadWorld(worldP, object->center, &payloads[object->payloadIndex]);
    }

    (void)leafIndex;
    return sdfAccelEvalPayloadWorld(worldP, object->center, &payloads[object->payloadIndex]);
}

SDF_ACCEL_CORE_FN void sdfAccelBvhQueryPoint(
    SdfFloat3 worldP,
    float* bestDistance,
    const SdfBvhNode* bvhNodes,
    uint32_t nodeIndex,
    const SdfAccelObjectGpu* objects,
    const SdfOctreeNode* octreeNodes,
    const SdfAccelPayloadGpu* payloads)
{
    if (bvhNodes == nullptr || bestDistance == nullptr || objects == nullptr || octreeNodes == nullptr
        || payloads == nullptr) {
        return;
    }

    const SdfBvhNode& node = bvhNodes[nodeIndex];
    const float aabbDist = sdfAccelPointAabbDistance(worldP, node.boundsMin, node.boundsMax);
    if (aabbDist >= *bestDistance) {
        return;
    }

    if ((node.flags & SdfBvhFlagLeaf) != 0) {
        const SdfAccelObjectGpu& object = objects[node.objectIndex];
        const float d = sdfAccelObjectSDF(worldP, &object, octreeNodes, payloads);
        *bestDistance = sdfMin2(*bestDistance, d);
        return;
    }

    sdfAccelBvhQueryPoint(worldP, bestDistance, bvhNodes, node.leftIndex, objects, octreeNodes, payloads);
    sdfAccelBvhQueryPoint(worldP, bestDistance, bvhNodes, node.rightIndex, objects, octreeNodes, payloads);
}

SDF_ACCEL_CORE_FN float sdfAccelSceneSDF(SdfFloat3 worldP, const SdfAccelSceneGpu* scene)
{
    if (scene == nullptr || scene->objectCount == 0 || scene->bvhNodeCount == 0) {
        return 1.0e20f;
    }

    float best = 1.0e20f;
    sdfAccelBvhQueryPoint(
        worldP,
        &best,
        scene->bvhNodes,
        scene->bvhRootIndex,
        scene->objects,
        scene->octreeNodes,
        scene->payloads);
    return best;
}
