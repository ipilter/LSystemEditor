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
