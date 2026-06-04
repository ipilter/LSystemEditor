#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelTypes.h"

MATH_CORE_FN Vec3 renderMissBackground(const RenderParamsGpu* params)
{
    if (params == nullptr) {
        return vecMake3(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f);
    }

    return vecMake3(params->backgroundR, params->backgroundG, params->backgroundB);
}

MATH_CORE_FN Vec3 normalToColor(Vec3 normal, bool hit, const RenderParamsGpu* params)
{
    if (!hit) {
        return renderMissBackground(params);
    }

    return vecMake3(
        normal.x * 0.5f + 0.5f,
        normal.y * 0.5f + 0.5f,
        normal.z * 0.5f + 0.5f);
}

MATH_CORE_FN Vec3 distanceToHeatmap(float t, float maxDist, bool hit, const RenderParamsGpu* params)
{
    if (!hit) {
        return renderMissBackground(params);
    }

    const float u = vecMin2(t / maxDist, 1.0f);
    const float r = vecMin2(u * 3.0f, 1.0f);
    const float g = vecMax2(0.0f, vecMin2(u * 3.0f - 1.0f, 1.0f));
    const float b = vecMax2(0.0f, 1.0f - u * 2.0f);
    return vecMake3(r, g, b);
}
