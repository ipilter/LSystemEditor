#pragma once

#include "SdfAccelScene.h"
#include "SdfAccelTraverseCore.h"
#include "SdfAccelRayMarchCore.h"
#include "SdfMathCore.h"
#include "SdfSceneContent.h"

#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace SdfAccelTest {

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

inline float accelSceneEvalSDF(const SdfAccelScene& scene, SdfFloat3 p)
{
    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    if (hostScene == nullptr) {
        return 1.0e20f;
    }
    return sdfAccelSceneSDF(p, hostScene);
}

inline SdfHit accelSceneRayMarch(
    const SdfAccelScene& scene,
    SdfFloat3 ro,
    SdfFloat3 rd,
    const SdfMarchParamsGpu& params)
{
    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    if (hostScene == nullptr) {
        return SdfHit{};
    }
    return sdfAccelRayMarch(ro, rd, hostScene, &params);
}

inline float bruteSceneSDF(SdfFloat3 p, const std::vector<std::unique_ptr<SdfShape>>& shapes)
{
    float best = 1.0e20f;
    for (const std::unique_ptr<SdfShape>& shape : shapes) {
        if (shape != nullptr) {
            best = sdfMin2(best, shape->evalWorld(p));
        }
    }
    return best;
}

inline SdfFloat3 evalRay(SdfFloat3 ro, SdfFloat3 rd, float t)
{
    return sdfEvalRay(ro, rd, t);
}

inline SdfHit bruteForceRayMarch(
    SdfFloat3 ro,
    SdfFloat3 rd,
    const std::vector<std::unique_ptr<SdfShape>>& shapes,
    const SdfMarchParamsGpu& params)
{
    SdfHit result{};
    if (shapes.empty()) {
        return result;
    }

    rd = sdfNormalize3(rd);
    if (bruteSceneSDF(ro, shapes) < -params.surfaceEpsilon) {
        return result;
    }

    const float coarseDt = 0.02f;
    float t = 0.0f;
    float prevSdf = bruteSceneSDF(ro, shapes);
    float tPrev = 0.0f;
    int steps = 0;
    const int maxCoarseSteps = static_cast<int>(params.maxDistance / coarseDt) + 1;

    while (t <= params.maxDistance && steps < maxCoarseSteps) {
        t += coarseDt;
        const SdfFloat3 p = evalRay(ro, rd, t);
        const float d = bruteSceneSDF(p, shapes);
        if (prevSdf > params.surfaceEpsilon && d <= params.surfaceEpsilon) {
            float t0 = tPrev;
            float t1 = t;
            for (int i = 0; i < 20; ++i) {
                const float tMid = 0.5f * (t0 + t1);
                const float midSdf = bruteSceneSDF(evalRay(ro, rd, tMid), shapes);
                if (midSdf > params.surfaceEpsilon) {
                    t0 = tMid;
                } else {
                    t1 = tMid;
                }
            }
            result.hit = true;
            result.t = t1;
            result.steps = steps + 1;
            result.sdfAtHit = bruteSceneSDF(evalRay(ro, rd, t1), shapes);
            return result;
        }
        tPrev = t;
        prevSdf = d;
        ++steps;
    }

    return result;
}

inline bool octreeDescentOk(
    SdfFloat3 localP,
    const SdfOctreeNode* nodes,
    uint32_t rootIndex)
{
    uint32_t leafIndex = 0;
    return sdfAccelOctreeDescendToLeaf(localP, nodes, rootIndex, &leafIndex);
}

} // namespace SdfAccelTest
