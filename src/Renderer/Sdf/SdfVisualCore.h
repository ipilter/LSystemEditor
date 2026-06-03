#pragma once

#include "SdfMathCore.h"
#include "SdfTypes.h"

SDF_CORE_FN SdfFloat3 sdfMissBackground(const SdfMarchParamsGpu* params)
{
    if (params == nullptr) {
        return sdfMakeFloat3(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f);
    }

    return sdfMakeFloat3(params->backgroundR, params->backgroundG, params->backgroundB);
}

SDF_CORE_FN SdfFloat3 stepsToHeatmap(int steps, int maxSteps, bool hit, const SdfMarchParamsGpu* params)
{
    if (!hit && steps <= 0) {
        return sdfMissBackground(params);
    }

    if (maxSteps <= 0) {
        return sdfMissBackground(params);
    }

    const float u = sdfClamp(static_cast<float>(steps) / static_cast<float>(maxSteps), 0.0f, 1.0f);
    const float r = sdfMin2(u * 4.0f, 1.0f);
    const float g = sdfMax2(0.0f, sdfMin2(u * 4.0f - 1.0f, 1.0f));
    const float b = sdfMax2(0.0f, 1.0f - u * 2.5f);
    return sdfMakeFloat3(r, g, b);
}

SDF_CORE_FN SdfFloat3 normalToColor(SdfFloat3 normal, bool hit, const SdfMarchParamsGpu* params)
{
    if (!hit) {
        return sdfMissBackground(params);
    }

    return sdfMakeFloat3(
        normal.x * 0.5f + 0.5f,
        normal.y * 0.5f + 0.5f,
        normal.z * 0.5f + 0.5f);
}

SDF_CORE_FN SdfFloat3 distanceToHeatmap(float t, float maxDist, bool hit, const SdfMarchParamsGpu* params)
{
    if (!hit) {
        return sdfMissBackground(params);
    }

    const float u = sdfMin2(t / maxDist, 1.0f);
    const float r = sdfMin2(u * 3.0f, 1.0f);
    const float g = sdfMax2(0.0f, sdfMin2(u * 3.0f - 1.0f, 1.0f));
    const float b = sdfMax2(0.0f, 1.0f - u * 2.0f);
    return sdfMakeFloat3(r, g, b);
}
