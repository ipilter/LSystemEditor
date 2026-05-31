#pragma once

#include "SdfRayMarcherCore.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

namespace SdfTest {

inline int& failureCount()
{
    static int count = 0;
    return count;
}

inline void expectTrue(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failureCount();
    }
}

inline void expectNear(float actual, float expected, float tolerance, const char* message)
{
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " (actual=" << actual << " expected=" << expected << ")\n";
        ++failureCount();
    }
}

struct AnalyticHit
{
    bool hit = false;
    float t = 0.0f;
};

inline SdfFloat3 evalRay(SdfFloat3 ro, SdfFloat3 rd, float t)
{
    return sdfEvalRay(ro, rd, t);
}

inline SdfMarchParamsGpu defaultMarchParams()
{
    SdfMarchParamsGpu params{};
    params.maxDistance = 100.0f;
    params.surfaceEpsilon = 1.0e-4f;
    params.normalEpsilon = 1.0e-4f;
    params.maxSteps = 256;
    params.refineIterations = 10;
    return params;
}

inline SdfSceneGpu defaultScene()
{
    SdfSceneGpu scene{};
    scene.cylinderCenter = sdfMakeFloat3(0.0f, 0.0f, 0.0f);
    scene.cylinderHalfExtents = sdfMakeFloat2(0.5f, 1.0f);
    scene.sphereCenter = sdfMakeFloat3(1.0e6f, 0.0f, 0.0f);
    scene.sphereRadius = 0.5f;
    scene.coneCenter = sdfMakeFloat3(1.0e6f, 0.0f, 0.0f);
    scene.coneHalfHeight = 1.0f;
    scene.coneRadiusBottom = 0.5f;
    scene.coneRadiusTop = 0.125f;
    return scene;
}

inline bool isOutsideStart(SdfFloat3 ro, const SdfSceneGpu* scene, float surfaceEpsilon)
{
    return sceneSDF(ro, scene) >= -surfaceEpsilon;
}

inline AnalyticHit analyticCylinderHit(
    SdfFloat3 ro,
    SdfFloat3 rd,
    SdfFloat3 center,
    SdfFloat2 h,
    float maxDistance)
{
    AnalyticHit best{};

    ro = sdfSub3(ro, center);
    rd = sdfNormalize3(rd);

    const float a = rd.x * rd.x + rd.z * rd.z;
    const float b = 2.0f * (ro.x * rd.x + ro.z * rd.z);
    const float c = ro.x * ro.x + ro.z * ro.z - h.x * h.x;
    const float discriminant = b * b - 4.0f * a * c;

    auto consider = [&](float t) {
        if (t < 0.0f || t > maxDistance) {
            return;
        }
        const float y = ro.y + t * rd.y;
        if (std::abs(y) > h.y) {
            return;
        }
        if (!best.hit || t < best.t) {
            best.hit = true;
            best.t = t;
        }
    };

    if (a > 1.0e-12f && discriminant >= 0.0f) {
        const float sqrtDisc = std::sqrt(discriminant);
        consider((-b - sqrtDisc) / (2.0f * a));
        consider((-b + sqrtDisc) / (2.0f * a));
    }

    if (std::abs(rd.y) > 1.0e-12f) {
        consider((h.y - ro.y) / rd.y);
        consider((-h.y - ro.y) / rd.y);
    }

    if (best.hit) {
        const SdfFloat3 hitLocal = evalRay(ro, rd, best.t);
        const float radial = std::sqrt(hitLocal.x * hitLocal.x + hitLocal.z * hitLocal.z);
        if (radial > h.x + 1.0e-4f) {
            best.hit = false;
            best.t = 0.0f;
        }
    }

    return best;
}

inline SdfHit bruteForceMarch(
    SdfFloat3 ro,
    SdfFloat3 rd,
    const SdfSceneGpu* scene,
    const SdfMarchParamsGpu* params)
{
    SdfHit result{};
    if (scene == nullptr || params == nullptr) {
        return result;
    }

    rd = sdfNormalize3(rd);
    if (!isOutsideStart(ro, scene, params->surfaceEpsilon)) {
        return result;
    }

    const float coarseDt = 0.02f;
    float t = 0.0f;
    float prevSdf = sceneSDF(ro, scene);
    float tPrev = 0.0f;
    int steps = 0;
    const int maxCoarseSteps = static_cast<int>(params->maxDistance / coarseDt) + 1;

    while (t <= params->maxDistance && steps < maxCoarseSteps) {
        t += coarseDt;
        const SdfFloat3 p = evalRay(ro, rd, t);
        const float d = sceneSDF(p, scene);
        if (prevSdf > params->surfaceEpsilon && d <= params->surfaceEpsilon) {
            float t0 = tPrev;
            float t1 = t;
            for (int i = 0; i < 20; ++i) {
                const float tMid = 0.5f * (t0 + t1);
                const float midSdf = sceneSDF(evalRay(ro, rd, tMid), scene);
                if (midSdf > params->surfaceEpsilon) {
                    t0 = tMid;
                } else {
                    t1 = tMid;
                }
            }
            result.hit = true;
            result.t = t1;
            result.steps = steps + 1;
            result.sdfAtHit = sceneSDF(evalRay(ro, rd, t1), scene);
            return result;
        }
        tPrev = t;
        prevSdf = d;
        ++steps;
    }

    return result;
}

inline void expectHitInvariants(
    SdfFloat3 ro,
    SdfFloat3 rd,
    const SdfHit& hit,
    const SdfSceneGpu* scene,
    const SdfMarchParamsGpu* params,
    const char* message)
{
    if (!hit.hit) {
        return;
    }

    rd = sdfNormalize3(rd);
    const SdfFloat3 hitPoint = evalRay(ro, rd, hit.t);
    const float sdf = sceneSDF(hitPoint, scene);
    expectTrue(std::abs(sdf) <= params->surfaceEpsilon * 4.0f, message);
    expectTrue(hit.t <= params->maxDistance, message);
    expectTrue(hit.steps > 0 && hit.steps <= params->maxSteps, message);

    const float beforeT = sdfMax2(0.0f, hit.t - 1.0e-3f);
    const SdfFloat3 beforePoint = evalRay(ro, rd, beforeT);
    expectTrue(sceneSDF(beforePoint, scene) > 0.0f, message);
}

} // namespace SdfTest
