#pragma once

#include "SdfAccelTraverseCore.h"
#include "Sdf/SdfTypes.h"

SDF_ACCEL_CORE_FN SdfFloat3 sdfAccelNormalAt(SdfFloat3 p, const SdfAccelSceneGpu* scene, float eps)
{
    const float d = sdfAccelSceneSDFExact(p, scene);
    const SdfFloat3 n = sdfMakeFloat3(
        sdfAccelSceneSDFExact(sdfAdd3(p, sdfMakeFloat3(eps, 0.0f, 0.0f)), scene) - d,
        sdfAccelSceneSDFExact(sdfAdd3(p, sdfMakeFloat3(0.0f, eps, 0.0f)), scene) - d,
        sdfAccelSceneSDFExact(sdfAdd3(p, sdfMakeFloat3(0.0f, 0.0f, eps)), scene) - d);
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
        if (sdfAccelSceneSDFExact(pMid, scene) > eps) {
            t0 = tMid;
        } else {
            t1 = tMid;
        }
    }

    return t1;
}

SDF_ACCEL_CORE_FN SdfHit sdfAccelRayMarchBrute(
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

    const float originSdf = sdfAccelSceneSDFExact(ro, scene);
    if (originSdf < -params->surfaceEpsilon) {
        return result;
    }

    float t = 0.0f;
    float tPrev = 0.0f;
    int steps = 0;

    while (steps < params->maxSteps) {
        const SdfFloat3 p = sdfEvalRay(ro, rd, t);
        const float d = sdfAccelSceneSDFBrute(p, scene);

        if (d <= params->surfaceEpsilon) {
            const float tHit = sdfAccelRefineHit(ro, rd, tPrev, t, scene, params);
            const SdfFloat3 hitPoint = sdfEvalRay(ro, rd, tHit);
            result.hit = true;
            result.t = tHit;
            result.steps = steps + 1;
            result.sdfAtHit = sdfAccelSceneSDFExact(hitPoint, scene);
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

SDF_ACCEL_CORE_FN SdfHit sdfAccelRayMarchBvh(
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

    const float originSdf = sdfAccelSceneSDFExact(ro, scene);
    if (originSdf < -params->surfaceEpsilon) {
        return result;
    }

    float t = 0.0f;
    float tPrev = 0.0f;
    int steps = 0;

    const float exactThreshold = params->exactSwitchThreshold > 0.0f
        ? params->exactSwitchThreshold
        : params->surfaceEpsilon;

    while (steps < params->maxSteps) {
        const SdfFloat3 p = sdfEvalRay(ro, rd, t);
        const float dCons = sdfAccelSceneSDFConservative(p, scene);

        float dExact = dCons;
        bool needExact = dCons <= exactThreshold || dCons <= params->surfaceEpsilon;
        if (needExact) {
            dExact = sdfAccelSceneSDFExact(p, scene);
            if (dExact <= params->surfaceEpsilon) {
                const float tHit = sdfAccelRefineHit(ro, rd, tPrev, t, scene, params);
                const SdfFloat3 hitPoint = sdfEvalRay(ro, rd, tHit);
                result.hit = true;
                result.t = tHit;
                result.steps = steps + 1;
                result.sdfAtHit = sdfAccelSceneSDFExact(hitPoint, scene);
                result.normal = sdfAccelNormalAt(hitPoint, scene, params->normalEpsilon);
                return result;
            }
        } else if (dCons <= params->surfaceEpsilon) {
            const float tHit = sdfAccelRefineHit(ro, rd, tPrev, t, scene, params);
            const SdfFloat3 hitPoint = sdfEvalRay(ro, rd, tHit);
            result.hit = true;
            result.t = tHit;
            result.steps = steps + 1;
            result.sdfAtHit = sdfAccelSceneSDFExact(hitPoint, scene);
            result.normal = sdfAccelNormalAt(hitPoint, scene, params->normalEpsilon);
            return result;
        }

        float d = sdfMax2(0.0f, needExact ? sdfMax2(dCons, dExact) : dCons);
        if (d <= 0.0f) {
            d = params->surfaceEpsilon;
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

SDF_ACCEL_CORE_FN SdfHit sdfAccelRayMarch(
    SdfFloat3 ro,
    SdfFloat3 rd,
    const SdfAccelSceneGpu* scene,
    const SdfMarchParamsGpu* params,
    int traversalMode)
{
    if (traversalMode == static_cast<int>(SdfTraversalMode::BruteForce)) {
        return sdfAccelRayMarchBrute(ro, rd, scene, params);
    }
    return sdfAccelRayMarchBvh(ro, rd, scene, params);
}
