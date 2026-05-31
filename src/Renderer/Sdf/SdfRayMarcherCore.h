#pragma once

#include "SdfPrimitivesCore.h"

SDF_CORE_FN SdfFloat3 sdfNormalAt(SdfFloat3 p, const SdfSceneGpu* scene, float eps)
{
    const float d = sceneSDF(p, scene);
    const SdfFloat3 n = sdfMakeFloat3(
        sceneSDF(sdfAdd3(p, sdfMakeFloat3(eps, 0.0f, 0.0f)), scene) - d,
        sceneSDF(sdfAdd3(p, sdfMakeFloat3(0.0f, eps, 0.0f)), scene) - d,
        sceneSDF(sdfAdd3(p, sdfMakeFloat3(0.0f, 0.0f, eps)), scene) - d);
    return sdfNormalize3(n);
}

SDF_CORE_FN float sdfRefineHit(
    SdfFloat3 ro,
    SdfFloat3 rd,
    float tOutside,
    float tInside,
    const SdfSceneGpu* scene,
    const SdfMarchParamsGpu* params)
{
    float t0 = tOutside;
    float t1 = tInside;
    const int iterations = params != nullptr && params->refineIterations > 0 ? params->refineIterations : 10;
    const float eps = params != nullptr ? params->surfaceEpsilon : 1.0e-4f;

    for (int i = 0; i < iterations; ++i) {
        const float tMid = 0.5f * (t0 + t1);
        const SdfFloat3 pMid = sdfEvalRay(ro, rd, tMid);
        if (sceneSDF(pMid, scene) > eps) {
            t0 = tMid;
        } else {
            t1 = tMid;
        }
    }

    return t1;
}

SDF_CORE_FN SdfHit sdfRayMarch(SdfFloat3 ro, SdfFloat3 rd, const SdfSceneGpu* scene, const SdfMarchParamsGpu* params)
{
    SdfHit result{};

    if (scene == nullptr || params == nullptr) {
        return result;
    }

    rd = sdfNormalize3(rd);

    const float originSdf = sceneSDF(ro, scene);
    if (originSdf < -params->surfaceEpsilon) {
        return result;
    }

    float t = 0.0f;
    float tPrev = 0.0f;
    int steps = 0;

    while (steps < params->maxSteps) {
        const SdfFloat3 p = sdfEvalRay(ro, rd, t);
        const float d = sceneSDF(p, scene);

        if (d <= params->surfaceEpsilon) {
            const float tHit = sdfRefineHit(ro, rd, tPrev, t, scene, params);
            const SdfFloat3 hitPoint = sdfEvalRay(ro, rd, tHit);
            result.hit = true;
            result.t = tHit;
            result.steps = steps + 1;
            result.sdfAtHit = sceneSDF(hitPoint, scene);
            result.normal = sdfNormalAt(hitPoint, scene, params->normalEpsilon);
            return result;
        }

        tPrev = t;
        t += d;

        if (t >= params->maxDistance) {
            return result;
        }

        ++steps;
    }

    return result;
}

SDF_CORE_FN SdfFloat3 distanceToHeatmap(float t, float maxDist, bool hit)
{
    if (!hit) {
        return sdfMakeFloat3(0.02f, 0.02f, 0.05f);
    }

    const float u = sdfMin2(t / maxDist, 1.0f);
    const float r = sdfMin2(u * 3.0f, 1.0f);
    const float g = sdfMax2(0.0f, sdfMin2(u * 3.0f - 1.0f, 1.0f));
    const float b = sdfMax2(0.0f, 1.0f - u * 2.0f);
    return sdfMakeFloat3(r, g, b);
}
