#pragma once

#include "SdfAccelBoundsCore.h"
#include "SdfAccelField.h"
#include "Sdf/SdfMathCore.h"
#include "Sdf/SdfPrimitiveType.h"

SDF_ACCEL_CORE_FN bool sdfAccelPayloadIsAnalytical(const SdfAccelPayloadGpu* payload)
{
    if (payload == nullptr) {
        return false;
    }

    switch (static_cast<SdfPrimitiveType>(payload->type)) {
    case SdfPrimitiveType::Sphere:
    case SdfPrimitiveType::Cylinder:
    case SdfPrimitiveType::CappedCone:
        return true;
    default:
        return false;
    }
}

SDF_ACCEL_CORE_FN bool sdfAccelObjectIsAnalytical(
    const SdfAccelObjectGpu* object,
    const SdfAccelPayloadGpu* payloads)
{
    if (object == nullptr) {
        return false;
    }
    if ((object->flags & SdfObjectFlagAnalytical) != 0) {
        return true;
    }
    return payloads != nullptr && sdfAccelPayloadIsAnalytical(&payloads[object->payloadIndex]);
}

SDF_ACCEL_CORE_FN float sdfAccelEvalPayloadWorld(
    SdfFloat3 worldP,
    SdfFloat3 objectCenter,
    const SdfAccelPayloadGpu* payload)
{
    if (payload == nullptr) {
        return 1.0e20f;
    }

    const SdfFloat3 localP = sdfSub3(worldP, objectCenter);
    switch (static_cast<SdfPrimitiveType>(payload->type)) {
    case SdfPrimitiveType::Sphere:
        return sdSphere(localP, payload->param0);
    case SdfPrimitiveType::Cylinder:
        return sdCylinder(localP, payload->halfExtents);
    case SdfPrimitiveType::CappedCone:
        return sdCappedCone(localP, payload->param0, payload->param1, payload->param2);
    default:
        return 1.0e20f;
    }
}

SDF_ACCEL_CORE_FN float sdfAccelObjectConservativeMin(
    SdfFloat3 worldP,
    const SdfAccelObjectGpu* object,
    const SdfAccelPayloadGpu* payloads)
{
    if (object == nullptr) {
        return 1.0e20f;
    }

    if (sdfAccelObjectIsAnalytical(object, payloads)) {
        return payloads != nullptr
            ? sdfAccelEvalPayloadWorld(worldP, object->center, &payloads[object->payloadIndex])
            : 1.0e20f;
    }

    return sdfAccelPointAabbDistance(worldP, object->boundsMin, object->boundsMax);
}

SDF_ACCEL_CORE_FN float sdfAccelObjectSDF(
    SdfFloat3 worldP,
    const SdfAccelObjectGpu* object,
    const SdfAccelPayloadGpu* payloads)
{
    if (object == nullptr || payloads == nullptr) {
        return 1.0e20f;
    }

    return sdfAccelEvalPayloadWorld(worldP, object->center, &payloads[object->payloadIndex]);
}

SDF_ACCEL_CORE_FN void sdfAccelBvhQueryPointExact(
    SdfFloat3 worldP,
    float* bestDistance,
    const SdfBvhNode* bvhNodes,
    uint32_t nodeIndex,
    const SdfAccelObjectGpu* objects,
    const SdfAccelPayloadGpu* payloads)
{
    if (bvhNodes == nullptr || bestDistance == nullptr || objects == nullptr || payloads == nullptr) {
        return;
    }

    const SdfBvhNode& node = bvhNodes[nodeIndex];
    const float aabbDist = sdfAccelPointAabbDistance(worldP, node.boundsMin, node.boundsMax);
    if (aabbDist >= *bestDistance) {
        return;
    }

    if ((node.flags & SdfBvhFlagLeaf) != 0) {
        const SdfAccelObjectGpu& object = objects[node.objectIndex];
        const float d = sdfAccelObjectSDF(worldP, &object, payloads);
        *bestDistance = sdfMin2(*bestDistance, d);
        return;
    }

    sdfAccelBvhQueryPointExact(worldP, bestDistance, bvhNodes, node.leftIndex, objects, payloads);
    sdfAccelBvhQueryPointExact(worldP, bestDistance, bvhNodes, node.rightIndex, objects, payloads);
}

SDF_ACCEL_CORE_FN void sdfAccelBvhQueryPointConservative(
    SdfFloat3 worldP,
    float* bestDistance,
    const SdfBvhNode* bvhNodes,
    uint32_t nodeIndex,
    const SdfAccelObjectGpu* objects,
    const SdfAccelPayloadGpu* payloads)
{
    if (bvhNodes == nullptr || bestDistance == nullptr || objects == nullptr || payloads == nullptr) {
        return;
    }

    const SdfBvhNode& node = bvhNodes[nodeIndex];
    const float aabbDist = sdfAccelPointAabbDistance(worldP, node.boundsMin, node.boundsMax);
    if (aabbDist >= *bestDistance) {
        return;
    }

    if ((node.flags & SdfBvhFlagLeaf) != 0) {
        const SdfAccelObjectGpu& object = objects[node.objectIndex];
        const float conservative = sdfAccelObjectConservativeMin(worldP, &object, payloads);
        *bestDistance = sdfMin2(*bestDistance, conservative);
        return;
    }

    sdfAccelBvhQueryPointConservative(
        worldP, bestDistance, bvhNodes, node.leftIndex, objects, payloads);
    sdfAccelBvhQueryPointConservative(
        worldP, bestDistance, bvhNodes, node.rightIndex, objects, payloads);
}

SDF_ACCEL_CORE_FN float sdfAccelSceneSDFBrute(SdfFloat3 worldP, const SdfAccelSceneGpu* scene)
{
    if (scene == nullptr || scene->objectCount == 0 || scene->objects == nullptr || scene->payloads == nullptr) {
        return 1.0e20f;
    }

    float best = 1.0e20f;
    for (uint32_t i = 0; i < scene->objectCount; ++i) {
        const float d = sdfAccelObjectSDF(worldP, &scene->objects[i], scene->payloads);
        best = sdfMin2(best, d);
    }
    return best;
}

SDF_ACCEL_CORE_FN float sdfAccelSceneSDFExact(SdfFloat3 worldP, const SdfAccelSceneGpu* scene)
{
    if (scene == nullptr || scene->objectCount == 0 || scene->bvhNodeCount == 0) {
        return 1.0e20f;
    }

    float best = 1.0e20f;
    sdfAccelBvhQueryPointExact(
        worldP,
        &best,
        scene->bvhNodes,
        scene->bvhRootIndex,
        scene->objects,
        scene->payloads);
    return best;
}

SDF_ACCEL_CORE_FN float sdfAccelSceneSDFConservative(SdfFloat3 worldP, const SdfAccelSceneGpu* scene)
{
    if (scene == nullptr || scene->objectCount == 0 || scene->bvhNodeCount == 0) {
        return 1.0e20f;
    }

    float best = 1.0e20f;
    sdfAccelBvhQueryPointConservative(
        worldP,
        &best,
        scene->bvhNodes,
        scene->bvhRootIndex,
        scene->objects,
        scene->payloads);
    return best;
}
