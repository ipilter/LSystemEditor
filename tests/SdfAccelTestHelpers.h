#pragma once

#include "SdfAccelScene.h"
#include "SdfAccelTraverseCore.h"
#include "SdfRayMarcherCore.h"

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

struct BruteObject
{
    SdfAccelPrimitiveType type = SdfAccelPrimitiveType::Sphere;
    SdfFloat3 center{};
    float param0 = 0.0f;
    float param1 = 0.0f;
    float param2 = 0.0f;
    SdfFloat2 halfExtents{};
};

inline float bruteEvalObject(SdfFloat3 p, const BruteObject& object)
{
    const SdfFloat3 localP = sdfSub3(p, object.center);
    switch (object.type) {
    case SdfAccelPrimitiveType::Sphere:
        return sdSphere(localP, object.param0);
    case SdfAccelPrimitiveType::Cylinder:
        return sdCylinder(localP, object.halfExtents);
    case SdfAccelPrimitiveType::CappedCone:
        return sdCappedCone(localP, object.param0, object.param1, object.param2);
    default:
        return 1.0e20f;
    }
}

inline float bruteSceneSDF(SdfFloat3 p, const std::vector<BruteObject>& objects)
{
    float best = 1.0e20f;
    for (const BruteObject& object : objects) {
        best = sdfMin2(best, bruteEvalObject(p, object));
    }
    return best;
}

inline std::vector<BruteObject> defaultLayoutObjects()
{
    std::vector<BruteObject> objects;
    objects.push_back(
        BruteObject{
            SdfAccelPrimitiveType::Cylinder,
            sdfMakeFloat3(-2.5f, 0.0f, 0.0f),
            0.0f,
            0.0f,
            0.0f,
            sdfMakeFloat2(0.5f, 1.0f)});
    objects.push_back(BruteObject{SdfAccelPrimitiveType::Sphere, sdfMakeFloat3(0.0f, 0.0f, 0.0f), 0.5f});
    objects.push_back(
        BruteObject{
            SdfAccelPrimitiveType::CappedCone,
            sdfMakeFloat3(2.5f, 0.0f, 0.0f),
            1.0f,
            0.5f,
            0.125f});
    return objects;
}

inline SdfSceneGpu defaultLayoutLegacyScene()
{
    SdfSceneGpu scene{};
    scene.cylinderCenter = sdfMakeFloat3(-2.5f, 0.0f, 0.0f);
    scene.cylinderHalfExtents = sdfMakeFloat2(0.5f, 1.0f);
    scene.sphereCenter = sdfMakeFloat3(0.0f, 0.0f, 0.0f);
    scene.sphereRadius = 0.5f;
    scene.coneCenter = sdfMakeFloat3(2.5f, 0.0f, 0.0f);
    scene.coneHalfHeight = 1.0f;
    scene.coneRadiusBottom = 0.5f;
    scene.coneRadiusTop = 0.125f;
    return scene;
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
