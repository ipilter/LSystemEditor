#pragma once

#include "SdfAccelTraverseCore.h"

SDF_ACCEL_CORE_FN SdfFloat3 sdfAccelNormalAt(SdfFloat3 p, const SdfAccelSceneGpu* scene, float eps)
{
    const float d = sdfAccelSceneSDF(p, scene);
    const SdfFloat3 n = sdfMakeFloat3(
        sdfAccelSceneSDF(sdfAdd3(p, sdfMakeFloat3(eps, 0.0f, 0.0f)), scene) - d,
        sdfAccelSceneSDF(sdfAdd3(p, sdfMakeFloat3(0.0f, eps, 0.0f)), scene) - d,
        sdfAccelSceneSDF(sdfAdd3(p, sdfMakeFloat3(0.0f, 0.0f, eps)), scene) - d);
    return sdfNormalize3(n);
}

SDF_ACCEL_CORE_FN float sdfAccelRefineHit(
    SdfFloat3 ro,
    SdfFloat3 rd,
    float tOutside,
    float tInside,
    const SdfAccelSceneGpu* scene,
    const SdfMarchParamsGpu* params)
{
    float t0 = tOutside;
    float t1 = tInside;
    const int iterations = params != nullptr && params->refineIterations > 0 ? params->refineIterations : 10;
    const float eps = params != nullptr ? params->surfaceEpsilon : 1.0e-4f;

    for (int i = 0; i < iterations; ++i) {
        const float tMid = 0.5f * (t0 + t1);
        const SdfFloat3 pMid = sdfEvalRay(ro, rd, tMid);
        if (sdfAccelSceneSDF(pMid, scene) > eps) {
            t0 = tMid;
        } else {
            t1 = tMid;
        }
    }

    return t1;
}

SDF_ACCEL_CORE_FN SdfHit sdfAccelRayMarch(
    SdfFloat3 ro,
    SdfFloat3 rd,
    const SdfAccelSceneGpu* scene,
    const SdfMarchParamsGpu* params)
{
    SdfHit result{};

    if (scene == nullptr || params == nullptr) {
        return result;
    }

    rd = sdfNormalize3(rd);

    const float originSdf = sdfAccelSceneSDF(ro, scene);
    if (originSdf < -params->surfaceEpsilon) {
        return result;
    }

    float t = 0.0f;
    float tPrev = 0.0f;
    int steps = 0;

    while (steps < params->maxSteps) {
        const SdfFloat3 p = sdfEvalRay(ro, rd, t);
        const float d = sdfAccelSceneSDF(p, scene);

        if (d <= params->surfaceEpsilon) {
            const float tHit = sdfAccelRefineHit(ro, rd, tPrev, t, scene, params);
            const SdfFloat3 hitPoint = sdfEvalRay(ro, rd, tHit);
            result.hit = true;
            result.t = tHit;
            result.steps = steps + 1;
            result.sdfAtHit = sdfAccelSceneSDF(hitPoint, scene);
            result.normal = sdfAccelNormalAt(hitPoint, scene, params->normalEpsilon);
            return result;
        }

        tPrev = t;
        t += d;

        if (t >= params->maxDistance) {
            result.steps = steps;
            return result;
        }

        ++steps;
    }

    result.steps = steps;
    return result;
}
