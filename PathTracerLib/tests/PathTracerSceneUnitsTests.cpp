#include "Geometry/MathCore.h"
#include "MeshAccel/Mesh.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelScene.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "PhysicalCamera.h"
#include "SceneUnits.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int gFailures = 0;

void expectTrue(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++gFailures;
    }
}

void expectNear(float actual, float expected, float tolerance, const char* message)
{
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " (actual=" << actual << ", expected=" << expected << ")\n";
        ++gFailures;
    }
}

Mesh buildBoxMesh(float halfExtent)
{
    Mesh mesh{};
    const Vec3 v000 = vecMake3(-halfExtent, -halfExtent, -halfExtent);
    const Vec3 v100 = vecMake3(halfExtent, -halfExtent, -halfExtent);
    const Vec3 v110 = vecMake3(halfExtent, halfExtent, -halfExtent);
    const Vec3 v010 = vecMake3(-halfExtent, halfExtent, -halfExtent);
    const Vec3 v001 = vecMake3(-halfExtent, -halfExtent, halfExtent);
    const Vec3 v101 = vecMake3(halfExtent, -halfExtent, halfExtent);
    const Vec3 v111 = vecMake3(halfExtent, halfExtent, halfExtent);
    const Vec3 v011 = vecMake3(-halfExtent, halfExtent, halfExtent);
    const Vec3 n = vecMake3(0.0f, 0.0f, 1.0f);

    auto addTri = [&](Vec3 a, Vec3 b, Vec3 c) {
        MeshTriangle tri{};
        tri.v0 = a;
        tri.v1 = b;
        tri.v2 = c;
        tri.n0 = n;
        tri.n1 = n;
        tri.n2 = n;
        mesh.triangles.push_back(tri);
    };

    addTri(v001, v101, v111);
    addTri(v001, v111, v011);
    (void)v000;
    (void)v100;
    (void)v110;
    (void)v010;
    return mesh;
}

void testCameraAndTracerShareRayRange()
{
    expectNear(
        PhysicalCamera::kDefaultFarPlane,
        SceneUnits::kDefaultRayTMaxMm,
        1.0e-3f,
        "camera far plane matches tracer ray tMax in mm");
    expectNear(
        PhysicalCamera::kDefaultFocusDistance,
        1000.0f,
        1.0e-3f,
        "default focus distance is 1 m in mm");
}

void testRayEpsilonScalesCoherently()
{
    expectNear(
        SceneUnits::rayEpsilonMm(0.0f, 0.0f),
        SceneUnits::kMinRayEpsilonMm,
        1.0e-6f,
        "primary epsilon floor at zero hit distance");

    const float at1500 = SceneUnits::rayEpsilonMm(1500.0f, 0.0f);
    expectNear(at1500, 1.5f, 1.0e-4f, "epsilon at 1500 mm hit distance");

    const float extentOnly = SceneUnits::rayEpsilonMm(0.0f, 25000.0f);
    expectNear(
        extentOnly,
        SceneUnits::kSceneExtentRayEpsilonScale * 25000.0f,
        1.0e-6f,
        "epsilon scales with scene extent");

    expectTrue(
        SceneUnits::rayEpsilonMm(500.0f, 5000.0f) >= SceneUnits::kMinRayEpsilonMm,
        "epsilon never drops below absolute floor");
}

void testNormalWeldCellUsesMmFloor()
{
    expectNear(
        SceneUnits::normalWeldCellSizeMm(10.0f),
        SceneUnits::kMinNormalWeldCellMm,
        1.0e-6f,
        "normal weld cell uses mm floor on small meshes");

    expectNear(
        SceneUnits::normalWeldCellSizeMm(25000.0f),
        SceneUnits::kNormalWeldExtentScale * 25000.0f,
        1.0e-4f,
        "normal weld cell scales with mesh extent");
}

void testSceneExtentMatchesMeshAabb()
{
    constexpr float kHalfExtent = 750.0f;
    Mesh mesh = buildBoxMesh(kHalfExtent);
    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "box mesh builds accel scene");

    const MeshAccelSceneGpu* sceneGpu = scene.hostScene();
    expectTrue(sceneGpu != nullptr, "host scene available");

    const MeshAabb aabb = meshComputeAabb(mesh);
    const float expectedExtent = vecMax3(
        aabb.max.x - aabb.min.x,
        aabb.max.y - aabb.min.y,
        aabb.max.z - aabb.min.z);
    expectNear(sceneGpu->sceneExtentMm, expectedExtent, 1.0e-3f, "scene extent matches mesh AABB");
}

void testPrimaryTraceUsesSharedEpsilon()
{
    constexpr float kHalfExtent = 500.0f;
    Mesh mesh = buildBoxMesh(kHalfExtent);
    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "box mesh builds for primary trace test");

    const MeshAccelSceneGpu* sceneGpu = scene.hostScene();
    const float primaryEpsilon = SceneUnits::rayEpsilonMm(0.0f, sceneGpu->sceneExtentMm);

    const Vec3 rayOrigin = vecMake3(0.0f, 0.0f, 2000.0f);
    const Vec3 rayDir = vecMake3(0.0f, 0.0f, -1.0f);
    const MeshHit hit = meshAccelTraceRay(
        rayOrigin, rayDir, sceneGpu, primaryEpsilon, SceneUnits::kDefaultRayTMaxMm);
    expectTrue(hit.hit, "vertical primary ray hits horizontal face through BVH");
    expectTrue(hit.t > primaryEpsilon, "primary hit lies beyond shared epsilon tMin");
}

} // namespace

int main()
{
    testCameraAndTracerShareRayRange();
    testRayEpsilonScalesCoherently();
    testNormalWeldCellUsesMmFloor();
    testSceneExtentMatchesMeshAabb();
    testPrimaryTraceUsesSharedEpsilon();

    if (gFailures != 0) {
        std::cerr << gFailures << " test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "All scene units tests passed\n";
    return EXIT_SUCCESS;
}
