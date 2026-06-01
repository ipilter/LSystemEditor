#pragma once

#include "SdfAccelTypes.h"
#include "SdfMathCore.h"
#include "SdfPrimitivesCore.h"

#include <cmath>

#if defined(__CUDACC__)
#define SDF_ACCEL_CORE_FN __host__ __device__ inline
#else
#define SDF_ACCEL_CORE_FN inline
#endif

struct SdfAccelAabb
{
    SdfFloat3 min{};
    SdfFloat3 max{};
};

struct SdfAccelNodeInterval
{
    float dMin = 0.0f;
    float dMax = 0.0f;
};

SDF_ACCEL_CORE_FN SdfAccelAabb sdfAccelMakeAabb(SdfFloat3 min, SdfFloat3 max)
{
    return SdfAccelAabb{min, max};
}

SDF_ACCEL_CORE_FN SdfAccelAabb sdfAccelExpandAabb(SdfAccelAabb aabb, float padding)
{
    const SdfFloat3 pad = sdfMakeFloat3(padding, padding, padding);
    return sdfAccelMakeAabb(sdfSub3(aabb.min, pad), sdfAdd3(aabb.max, pad));
}

SDF_ACCEL_CORE_FN SdfAccelAabb sdfAccelAabbFromCenterHalfExtent(SdfFloat3 center, SdfFloat3 halfExtent)
{
    return sdfAccelMakeAabb(sdfSub3(center, halfExtent), sdfAdd3(center, halfExtent));
}

SDF_ACCEL_CORE_FN float sdfAccelBoundRadius(SdfFloat3 halfExtent)
{
    return sdfLength3(halfExtent);
}

SDF_ACCEL_CORE_FN float sdfAccelMaxAxisHalfExtent(SdfFloat3 halfExtent)
{
    return sdfMax3(halfExtent.x, halfExtent.y, halfExtent.z);
}

SDF_ACCEL_CORE_FN float sdfAccelAutoMinNodeSize(SdfFloat3 halfExtent, int maxDepth)
{
    const float maxAxisExtent = 2.0f * sdfAccelMaxAxisHalfExtent(halfExtent);
    const int depth = maxDepth > 0 ? maxDepth : 1;
    const int divisor = 1 << depth;
    return maxAxisExtent / static_cast<float>(divisor);
}

SDF_ACCEL_CORE_FN SdfFloat3 sdfAccelAabbCorner(const SdfAccelAabb& aabb, int cornerIndex)
{
    return sdfMakeFloat3(
        (cornerIndex & 1) ? aabb.max.x : aabb.min.x,
        (cornerIndex & 2) ? aabb.max.y : aabb.min.y,
        (cornerIndex & 4) ? aabb.max.z : aabb.min.z);
}

SDF_ACCEL_CORE_FN SdfAccelNodeInterval sdfAccelComputeNodeInterval(
    SdfFloat3 center,
    SdfFloat3 halfExtent,
    float (*evalFn)(SdfFloat3, void*),
    void* evalContext)
{
    const SdfAccelAabb aabb = sdfAccelAabbFromCenterHalfExtent(center, halfExtent);
    const float boundRadius = sdfAccelBoundRadius(halfExtent);
    const float dCenter = evalFn(center, evalContext);

    float dCornerMin = dCenter;
    float dCornerMax = dCenter;
    for (int i = 0; i < 8; ++i) {
        const float dCorner = evalFn(sdfAccelAabbCorner(aabb, i), evalContext);
        dCornerMin = sdfMin2(dCornerMin, dCorner);
        dCornerMax = sdfMax2(dCornerMax, dCorner);
    }

    SdfAccelNodeInterval interval{};
    interval.dMin = sdfMin2(dCenter - boundRadius, dCornerMin);
    interval.dMax = sdfMax2(dCenter + boundRadius, dCornerMax);
    return interval;
}

SDF_ACCEL_CORE_FN bool sdfAccelPointInAabb(SdfFloat3 p, SdfFloat3 boundsMin, SdfFloat3 boundsMax)
{
    return p.x >= boundsMin.x && p.x <= boundsMax.x && p.y >= boundsMin.y && p.y <= boundsMax.y
        && p.z >= boundsMin.z && p.z <= boundsMax.z;
}

SDF_ACCEL_CORE_FN float sdfAccelPointAabbDistance(SdfFloat3 p, SdfFloat3 boundsMin, SdfFloat3 boundsMax)
{
    const float dx = sdfMax3(boundsMin.x - p.x, 0.0f, p.x - boundsMax.x);
    const float dy = sdfMax3(boundsMin.y - p.y, 0.0f, p.y - boundsMax.y);
    const float dz = sdfMax3(boundsMin.z - p.z, 0.0f, p.z - boundsMax.z);
    return sdfLength3(sdfMakeFloat3(dx, dy, dz));
}

SDF_ACCEL_CORE_FN int sdfAccelOctantIndex(SdfFloat3 p, SdfFloat3 center)
{
    int octant = 0;
    if (p.x >= center.x) {
        octant |= 1;
    }
    if (p.y >= center.y) {
        octant |= 2;
    }
    if (p.z >= center.z) {
        octant |= 4;
    }
    return octant;
}

SDF_ACCEL_CORE_FN SdfFloat3 sdfAccelChildCenter(SdfFloat3 parentCenter, SdfFloat3 parentHalfExtent, int octant)
{
    const float sx = (octant & 1) ? 1.0f : -1.0f;
    const float sy = (octant & 2) ? 1.0f : -1.0f;
    const float sz = (octant & 4) ? 1.0f : -1.0f;
    return sdfMakeFloat3(
        parentCenter.x + sx * parentHalfExtent.x * 0.5f,
        parentCenter.y + sy * parentHalfExtent.y * 0.5f,
        parentCenter.z + sz * parentHalfExtent.z * 0.5f);
}

SDF_ACCEL_CORE_FN SdfFloat3 sdfAccelChildHalfExtent(SdfFloat3 parentHalfExtent)
{
    return sdfScale3(parentHalfExtent, 0.5f);
}

SDF_ACCEL_CORE_FN SdfAccelAabb sdfAccelSphereBounds(SdfFloat3 center, float radius)
{
    const SdfFloat3 r = sdfMakeFloat3(radius, radius, radius);
    return sdfAccelMakeAabb(sdfSub3(center, r), sdfAdd3(center, r));
}

SDF_ACCEL_CORE_FN SdfAccelAabb sdfAccelCylinderBounds(SdfFloat3 center, SdfFloat2 halfExtents)
{
    return sdfAccelMakeAabb(
        sdfMakeFloat3(center.x - halfExtents.x, center.y - halfExtents.y, center.z - halfExtents.x),
        sdfMakeFloat3(center.x + halfExtents.x, center.y + halfExtents.y, center.z + halfExtents.x));
}

SDF_ACCEL_CORE_FN SdfAccelAabb sdfAccelCappedConeBounds(
    SdfFloat3 center,
    float halfHeight,
    float radiusBottom,
    float radiusTop)
{
    const float maxRadius = sdfMax2(radiusBottom, radiusTop);
    return sdfAccelMakeAabb(
        sdfMakeFloat3(center.x - maxRadius, center.y - halfHeight, center.z - maxRadius),
        sdfMakeFloat3(center.x + maxRadius, center.y + halfHeight, center.z + maxRadius));
}

SDF_ACCEL_CORE_FN SdfAccelAabb sdfAccelLocalBoundsFromPayload(const SdfAccelPayloadGpu* payload, SdfFloat3 worldCenter)
{
    if (payload == nullptr) {
        return sdfAccelMakeAabb(worldCenter, worldCenter);
    }

    switch (static_cast<SdfAccelPrimitiveType>(payload->type)) {
    case SdfAccelPrimitiveType::Sphere:
        return sdfAccelSphereBounds(worldCenter, payload->param0);
    case SdfAccelPrimitiveType::Cylinder:
        return sdfAccelCylinderBounds(worldCenter, payload->halfExtents);
    case SdfAccelPrimitiveType::CappedCone:
        return sdfAccelCappedConeBounds(worldCenter, payload->param0, payload->param1, payload->param2);
    default:
        return sdfAccelMakeAabb(worldCenter, worldCenter);
    }
}
