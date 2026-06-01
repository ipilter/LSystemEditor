#pragma once

#include "SdfAccelTypes.h"
#include "SdfAccelBoundsCore.h"

#include <functional>

struct SdfAccelField
{
    SdfFloat3 worldCenter{};
    SdfAccelPayloadGpu payload{};
    SdfFloat3 localBoundsMin{};
    SdfFloat3 localBoundsMax{};
    std::function<float(SdfFloat3)> evalLocal;
};

inline SdfAccelAabb sdfAccelFieldLocalAabb(const SdfAccelField& field, float padding)
{
    return sdfAccelExpandAabb(sdfAccelMakeAabb(field.localBoundsMin, field.localBoundsMax), padding);
}

inline float sdfAccelEvalPayloadLocal(SdfFloat3 localP, const SdfAccelPayloadGpu& payload)
{
    switch (static_cast<SdfAccelPrimitiveType>(payload.type)) {
    case SdfAccelPrimitiveType::Sphere:
        return sdSphere(localP, payload.param0);
    case SdfAccelPrimitiveType::Cylinder:
        return sdCylinder(localP, payload.halfExtents);
    case SdfAccelPrimitiveType::CappedCone:
        return sdCappedCone(localP, payload.param0, payload.param1, payload.param2);
    default:
        return 1.0e20f;
    }
}

inline SdfAccelField sdfAccelMakeSphereField(SdfFloat3 center, float radius)
{
    SdfAccelField field{};
    field.worldCenter = center;
    field.payload.type = static_cast<uint32_t>(SdfAccelPrimitiveType::Sphere);
    field.payload.param0 = radius;
    field.localBoundsMin = sdfSub3(center, sdfMakeFloat3(radius, radius, radius));
    field.localBoundsMax = sdfAdd3(center, sdfMakeFloat3(radius, radius, radius));
    field.evalLocal = [center, radius](SdfFloat3 worldP) {
        return sdSphere(sdfSub3(worldP, center), radius);
    };
    return field;
}

inline SdfAccelField sdfAccelMakeCylinderField(SdfFloat3 center, SdfFloat2 halfExtents)
{
    SdfAccelField field{};
    field.worldCenter = center;
    field.payload.type = static_cast<uint32_t>(SdfAccelPrimitiveType::Cylinder);
    field.payload.halfExtents = halfExtents;
    field.payload.param0 = halfExtents.x;
    field.payload.param1 = halfExtents.y;
    const SdfAccelAabb bounds = sdfAccelCylinderBounds(center, halfExtents);
    field.localBoundsMin = bounds.min;
    field.localBoundsMax = bounds.max;
    field.evalLocal = [center, halfExtents](SdfFloat3 worldP) {
        return sdCylinder(sdfSub3(worldP, center), halfExtents);
    };
    return field;
}

inline SdfAccelField sdfAccelMakeCappedConeField(
    SdfFloat3 center,
    float halfHeight,
    float radiusBottom,
    float radiusTop)
{
    SdfAccelField field{};
    field.worldCenter = center;
    field.payload.type = static_cast<uint32_t>(SdfAccelPrimitiveType::CappedCone);
    field.payload.param0 = halfHeight;
    field.payload.param1 = radiusBottom;
    field.payload.param2 = radiusTop;
    const SdfAccelAabb bounds = sdfAccelCappedConeBounds(center, halfHeight, radiusBottom, radiusTop);
    field.localBoundsMin = bounds.min;
    field.localBoundsMax = bounds.max;
    field.evalLocal = [center, halfHeight, radiusBottom, radiusTop](SdfFloat3 worldP) {
        return sdCappedCone(sdfSub3(worldP, center), halfHeight, radiusBottom, radiusTop);
    };
    return field;
}
