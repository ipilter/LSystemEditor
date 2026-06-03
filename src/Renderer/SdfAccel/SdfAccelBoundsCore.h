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
