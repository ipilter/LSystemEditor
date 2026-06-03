#pragma once

#include "SdfAccelScene.h"
#include "SdfAccelTraverseCore.h"
#include "SdfAccelRayMarchCore.h"
#include "Sdf/Shapes/SphereSdf.h"
#include "SdfMathCore.h"
#include "SdfSceneContent.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <limits>
#include <memory>
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
    params.exactSwitchThreshold = 0.1f;
    params.enableRaySort = 0;
    return params;
}

inline float accelSceneEvalSDF(const SdfAccelScene& scene, SdfFloat3 p)
{
    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    if (hostScene == nullptr) {
        return 1.0e20f;
    }
    return sdfAccelSceneSDFExact(p, hostScene);
}

inline float accelSceneEvalSDFConservative(const SdfAccelScene& scene, SdfFloat3 p)
{
    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    if (hostScene == nullptr) {
        return 1.0e20f;
    }
    return sdfAccelSceneSDFConservative(p, hostScene);
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
    return sdfAccelRayMarch(
        ro,
        rd,
        hostScene,
        &params,
        static_cast<int>(SdfTraversalMode::BvhAccel));
}

inline SdfHit accelSceneRayMarchBrute(
    const SdfAccelScene& scene,
    SdfFloat3 ro,
    SdfFloat3 rd,
    const SdfMarchParamsGpu& params)
{
    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    if (hostScene == nullptr) {
        return SdfHit{};
    }
    return sdfAccelRayMarchBrute(ro, rd, hostScene, &params);
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

struct SphereGridConfig
{
    int nx = 3;
    int ny = 4;
    int nz = 1;
    float spacing = 2.0f;
    float radius = 0.25f;
};

inline std::vector<std::unique_ptr<SdfShape>> makeSphereGridShapes(const SphereGridConfig& config)
{
    std::vector<std::unique_ptr<SdfShape>> shapes;
    for (int x = 0; x < config.nx; ++x) {
        for (int y = 0; y < config.ny; ++y) {
            for (int z = 0; z < config.nz; ++z) {
                const SdfFloat3 center = sdfMakeFloat3(
                    static_cast<float>(x) * config.spacing,
                    static_cast<float>(y) * config.spacing,
                    static_cast<float>(z) * config.spacing);
                shapes.push_back(std::make_unique<SphereSdf>(center, config.radius));
            }
        }
    }
    return shapes;
}

inline int sphereGridObjectCount(const SphereGridConfig& config)
{
    return config.nx * config.ny * config.nz;
}

struct MarchRay
{
    SdfFloat3 ro{};
    SdfFloat3 rd{};
};

inline SdfFloat3 sphereGridCenter(const SphereGridConfig& config)
{
    const float cx = static_cast<float>(config.nx - 1) * config.spacing * 0.5f;
    const float cy = static_cast<float>(config.ny - 1) * config.spacing * 0.5f;
    const float cz = static_cast<float>(config.nz - 1) * config.spacing * 0.5f;
    return sdfMakeFloat3(cx, cy, cz);
}

inline float sphereGridExtent(const SphereGridConfig& config)
{
    const float extentX = static_cast<float>(config.nx - 1) * config.spacing;
    const float extentY = static_cast<float>(config.ny - 1) * config.spacing;
    const float extentZ = static_cast<float>(config.nz - 1) * config.spacing;
    const float maxAxis = sdfMax3(extentX, extentY, extentZ);
    return maxAxis + config.radius + config.spacing;
}

inline std::vector<MarchRay> makeBenchmarkRays(int count, SdfFloat3 sceneCenter, float sceneExtent)
{
    std::vector<MarchRay> rays;
    rays.reserve(static_cast<size_t>(count));

    std::mt19937 rng(0xBEEFCAFEu);
    std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> elevationDist(-0.35f, 0.35f);
    std::uniform_real_distribution<float> distanceScaleDist(1.2f, 2.5f);

    const float standoff = sceneExtent * 2.0f + 4.0f;
    for (int i = 0; i < count; ++i) {
        const float azimuth = angleDist(rng);
        const float elevation = elevationDist(rng);
        const float dist = standoff * distanceScaleDist(rng);

        const float cosEl = cosf(elevation);
        const SdfFloat3 offset = sdfMakeFloat3(
            cosEl * cosf(azimuth) * dist,
            sinf(elevation) * dist,
            cosEl * sinf(azimuth) * dist);
        const SdfFloat3 ro = sdfAdd3(sceneCenter, offset);
        const SdfFloat3 rd = sdfNormalize3(sdfSub3(sceneCenter, ro));
        rays.push_back(MarchRay{ro, rd});
    }

    return rays;
}

struct TraversalBenchmarkResult
{
    double msTotal = 0.0;
    int rayCount = 0;
    int hitCount = 0;
    long long totalSteps = 0;
    int minSteps = std::numeric_limits<int>::max();
    int maxSteps = 0;
    double meanSteps = 0.0;
    int measureIterations = 0;
};

inline SdfHit marchRay(
    const SdfAccelScene& scene,
    const MarchRay& ray,
    const SdfMarchParamsGpu& params,
    SdfTraversalMode mode)
{
    if (mode == SdfTraversalMode::BruteForce) {
        return accelSceneRayMarchBrute(scene, ray.ro, ray.rd, params);
    }
    return accelSceneRayMarch(scene, ray.ro, ray.rd, params);
}

inline TraversalBenchmarkResult runTraversalBenchmark(
    const SdfAccelScene& scene,
    const std::vector<MarchRay>& rays,
    const SdfMarchParamsGpu& params,
    SdfTraversalMode mode,
    int warmupIters,
    int measureIters)
{
    TraversalBenchmarkResult result{};
    result.rayCount = static_cast<int>(rays.size());
    result.measureIterations = measureIters;

    if (result.rayCount == 0 || measureIters <= 0) {
        return result;
    }

    for (int w = 0; w < warmupIters; ++w) {
        for (const MarchRay& ray : rays) {
            (void)marchRay(scene, ray, params, mode);
        }
    }

    const auto start = std::chrono::steady_clock::now();
    for (int iter = 0; iter < measureIters; ++iter) {
        for (const MarchRay& ray : rays) {
            const SdfHit hit = marchRay(scene, ray, params, mode);
            result.totalSteps += hit.steps;
            if (hit.steps < result.minSteps) {
                result.minSteps = hit.steps;
            }
            if (hit.steps > result.maxSteps) {
                result.maxSteps = hit.steps;
            }
            if (iter == 0 && hit.hit) {
                ++result.hitCount;
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();

    result.msTotal =
        std::chrono::duration<double, std::milli>(end - start).count();
    const long long totalRays =
        static_cast<long long>(result.rayCount) * static_cast<long long>(measureIters);
    if (totalRays > 0) {
        result.meanSteps = static_cast<double>(result.totalSteps) / static_cast<double>(totalRays);
    }
    if (result.minSteps == std::numeric_limits<int>::max()) {
        result.minSteps = 0;
    }

    return result;
}

inline int countTraversalHitParityMismatches(
    const SdfAccelScene& scene,
    const std::vector<MarchRay>& rays,
    const SdfMarchParamsGpu& params)
{
    int mismatches = 0;
    for (const MarchRay& ray : rays) {
        const SdfHit bruteHit = marchRay(scene, ray, params, SdfTraversalMode::BruteForce);
        const SdfHit bvhHit = marchRay(scene, ray, params, SdfTraversalMode::BvhAccel);
        if (bruteHit.hit != bvhHit.hit) {
            ++mismatches;
            continue;
        }
        if (bruteHit.hit && bvhHit.hit && std::abs(bvhHit.t - bruteHit.t) > 1.0e-2f) {
            ++mismatches;
        }
    }
    return mismatches;
}

inline void verifyTraversalHitParity(
    const SdfAccelScene& scene,
    const std::vector<MarchRay>& rays,
    const SdfMarchParamsGpu& params,
    const char* labelPrefix)
{
    for (size_t i = 0; i < rays.size(); ++i) {
        const SdfHit bruteHit = marchRay(scene, rays[i], params, SdfTraversalMode::BruteForce);
        const SdfHit bvhHit = marchRay(scene, rays[i], params, SdfTraversalMode::BvhAccel);

        std::string hitParityMsg = std::string(labelPrefix) + "HitParity" + std::to_string(i);
        expectTrue(bruteHit.hit == bvhHit.hit, hitParityMsg.c_str());

        if (bruteHit.hit && bvhHit.hit) {
            std::string tParityMsg = std::string(labelPrefix) + "TParity" + std::to_string(i);
            expectNear(bvhHit.t, bruteHit.t, 1.0e-2f, tParityMsg.c_str());
        }
    }
}

inline void printTraversalBenchmarkReport(
    const char* sceneLabel,
    int objectCount,
    int rayCount,
    int warmupIters,
    int measureIters,
    const TraversalBenchmarkResult& brute,
    const TraversalBenchmarkResult& bvh)
{
    const long long bruteTotalRays =
        static_cast<long long>(rayCount) * static_cast<long long>(measureIters);
    const long long bvhTotalRays = bruteTotalRays;

    const double bruteMsPerRay = bruteTotalRays > 0 ? brute.msTotal / static_cast<double>(bruteTotalRays) : 0.0;
    const double bvhMsPerRay =
        bvhTotalRays > 0 ? bvh.msTotal / static_cast<double>(bvhTotalRays) : 0.0;

    const double stepRatio =
        bvh.meanSteps > 0.0 ? brute.meanSteps / bvh.meanSteps : 0.0;
    const double timeRatio = bvh.msTotal > 0.0 ? brute.msTotal / bvh.msTotal : 0.0;

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "[Benchmark] " << sceneLabel << " (" << objectCount << " objs) | " << rayCount
              << " rays x " << measureIters << " iters (warmup " << warmupIters << ")\n";
    std::cout << std::setprecision(3);
    std::cout << "  Brute:  steps/ray=" << brute.meanSteps << "  total_steps=" << brute.totalSteps
              << "  hits=" << brute.hitCount << "  time=" << brute.msTotal
              << " ms  (" << bruteMsPerRay << " ms/ray)\n";
    std::cout << "  BVH:    steps/ray=" << bvh.meanSteps << "  total_steps=" << bvh.totalSteps
              << "  hits=" << bvh.hitCount << "  time=" << bvh.msTotal
              << " ms  (" << bvhMsPerRay << " ms/ray)\n";
    std::cout << std::setprecision(2);
    std::cout << "  Ratio (brute/bvh): steps=" << stepRatio << "x  time=" << timeRatio << "x\n";
}

inline void runGridTraversalBenchmark(
    const SphereGridConfig& grid,
    const char* sceneLabel,
    int rayCount,
    int warmupIters,
    int measureIters,
    bool assertHitParity)
{
    auto shapes = makeSphereGridShapes(grid);
    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);

    expectTrue(scene.build(), (std::string(sceneLabel) + "Build").c_str());

    const SdfMarchParamsGpu params = defaultMarchParams();
    const SdfFloat3 center = sphereGridCenter(grid);
    const float extent = sphereGridExtent(grid);
    const std::vector<MarchRay> rays = makeBenchmarkRays(rayCount, center, extent);

    const int parityMismatches = countTraversalHitParityMismatches(scene, rays, params);
    if (assertHitParity) {
        expectTrue(parityMismatches == 0, (std::string(sceneLabel) + "HitParity").c_str());
        if (parityMismatches == 0) {
            verifyTraversalHitParity(scene, rays, params, sceneLabel);
        }
    } else if (parityMismatches > 0) {
        std::cout << "[Benchmark] " << sceneLabel << " hit parity mismatches: " << parityMismatches
                  << " / " << rayCount << " (timing report still printed)\n";
    }

    const TraversalBenchmarkResult brute = runTraversalBenchmark(
        scene,
        rays,
        params,
        SdfTraversalMode::BruteForce,
        warmupIters,
        measureIters);
    const TraversalBenchmarkResult bvh = runTraversalBenchmark(
        scene,
        rays,
        params,
        SdfTraversalMode::BvhAccel,
        warmupIters,
        measureIters);

    printTraversalBenchmarkReport(
        sceneLabel,
        sphereGridObjectCount(grid),
        rayCount,
        warmupIters,
        measureIters,
        brute,
        bvh);
}

} // namespace SdfAccelTest
