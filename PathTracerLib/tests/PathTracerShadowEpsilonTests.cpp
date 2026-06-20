#include "Geometry/MathCore.h"
#include "MeshAccel/Mesh.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelScene.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Sampling/LightSamplingCore.h"
#include "SceneUnits.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

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

Mesh buildFanTessellatedPlane(int segments, float radius)
{
    Mesh mesh{};
    const Vec3 center{0.0f, 0.0f, 0.0f};
    const Vec3 up{0.0f, 0.0f, 1.0f};

    for (int segment = 0; segment < segments; ++segment) {
        const float angle0 = static_cast<float>(segment) * 6.283185307f / static_cast<float>(segments);
        const float angle1 = static_cast<float>(segment + 1) * 6.283185307f / static_cast<float>(segments);

        const Vec3 rim0{
            radius * std::cos(angle0),
            radius * std::sin(angle0),
            0.0f};
        const Vec3 rim1{
            radius * std::cos(angle1),
            radius * std::sin(angle1),
            0.0f};

        MeshTriangle tri{};
        tri.v0 = center;
        tri.v1 = rim0;
        tri.v2 = rim1;
        tri.n0 = up;
        tri.n1 = up;
        tri.n2 = up;
        mesh.triangles.push_back(tri);
    }

    return mesh;
}

Mesh buildLargeGroundQuad(float halfExtent)
{
    Mesh mesh{};
    const Vec3 up{0.0f, 0.0f, 1.0f};
    const Vec3 v00 = vecMake3(-halfExtent, -halfExtent, 0.0f);
    const Vec3 v10 = vecMake3(halfExtent, -halfExtent, 0.0f);
    const Vec3 v11 = vecMake3(halfExtent, halfExtent, 0.0f);
    const Vec3 v01 = vecMake3(-halfExtent, halfExtent, 0.0f);

    MeshTriangle tri0{};
    tri0.v0 = v00;
    tri0.v1 = v10;
    tri0.v2 = v11;
    tri0.n0 = up;
    tri0.n1 = up;
    tri0.n2 = up;

    MeshTriangle tri1{};
    tri1.v0 = v00;
    tri1.v1 = v11;
    tri1.v2 = v01;
    tri1.n0 = up;
    tri1.n1 = up;
    tri1.n2 = up;

    mesh.triangles.push_back(tri0);
    mesh.triangles.push_back(tri1);
    return mesh;
}

void testRayEpsilonScalesWithHitDistanceAndExtent()
{
    expectNear(
        SceneUnits::rayEpsilonMm(0.0f, 0.0f),
        SceneUnits::kMinRayEpsilonMm,
        1.0e-6f,
        "ray epsilon uses absolute floor");

    expectNear(
        SceneUnits::rayEpsilonMm(1500.0f, 0.0f),
        SceneUnits::kRelativeRayEpsilon * 1500.0f,
        1.0e-4f,
        "ray epsilon scales with hit distance");

    expectNear(
        SceneUnits::rayEpsilonMm(0.0f, 25000.0f),
        SceneUnits::kSceneExtentRayEpsilonScale * 25000.0f,
        1.0e-6f,
        "ray epsilon scales with scene extent");
}

void testGroundQuadTraceAndShadow()
{
    Mesh mesh = buildLargeGroundQuad(1200.0f);
    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "ground quad mesh accel scene builds");

    const MeshAccelSceneGpu* sceneGpu = scene.hostScene();
    expectTrue(sceneGpu != nullptr, "ground quad host scene available");

    const Vec3 rayOrigin = vecMake3(250.0f, 250.0f, 500.0f);
    const Vec3 rayDir = vecMake3(0.0f, 0.0f, -1.0f);
    const float primaryEpsilon = SceneUnits::rayEpsilonMm(0.0f, sceneGpu->sceneExtentMm);
    const MeshHit hit = meshAccelTraceRay(rayOrigin, rayDir, sceneGpu, primaryEpsilon, 1.0e6f);
    expectTrue(hit.hit, "camera ray hits ground quad through BVH");
    expectTrue(hit.t > 100.0f, "ground quad hit distance is scene scale");

    const Vec3 position = vecEvalRay(rayOrigin, rayDir, hit.t);
    const Vec3 wi = vecMake3(0.2f, 0.1f, 0.97f);
    expectTrue(
        !lightIsOccluded(position, hit.normal, wi, sceneGpu, hit.t, hit.triangleIndex),
        "environment shadow ray does not self-occlude on ground quad");
}

void testFanPlaneEnvironmentShadowRaysDoNotSelfOcclude()
{
    constexpr int kSegments = 128;
    constexpr float kRadius = 1000.0f;
    constexpr float kHalfExtent = 1200.0f;

    Mesh mesh = buildFanTessellatedPlane(kSegments, kRadius);
    Mesh groundMesh = buildLargeGroundQuad(kHalfExtent);
    meshAppend(mesh, groundMesh, 0u);

    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "fan plane mesh accel scene builds");

    const MeshAccelSceneGpu* sceneGpu = scene.hostScene();
    expectTrue(sceneGpu != nullptr, "fan plane host scene available");
    expectTrue(sceneGpu->sceneExtentMm > 1500.0f, "fan plane scene extent is populated");
    expectTrue(sceneGpu->triangleCount > 0, "fan plane has triangles");

    const TriangleGpu& probeTri = scene.trianglesHost().front();
    const Vec3 probeOrigin = vecMake3(900.0f, 0.0f, 100.0f);
    const Vec3 probeDir = vecMake3(0.0f, 0.0f, -1.0f);
    float probeT = 0.0f;
    Vec3 probeNormal{};
    Vec2 probeUv{};
    expectTrue(
        meshAccelRayTriangle(probeOrigin, probeDir, probeTri, 0.0f, 1.0e6f, probeT, probeNormal, probeUv),
        "direct triangle intersection succeeds on fan plane wedge");

    const Vec3 rayOrigin = vecMake3(250.0f, 250.0f, 500.0f);
    const Vec3 rayDir = vecMake3(0.0f, 0.0f, -1.0f);
    const float primaryEpsilon = SceneUnits::rayEpsilonMm(0.0f, sceneGpu->sceneExtentMm);
    const MeshHit hit = meshAccelTraceRay(rayOrigin, rayDir, sceneGpu, primaryEpsilon, 1.0e6f);
    expectTrue(hit.hit, "camera ray hits fan plane");
    expectTrue(hit.t > 100.0f, "fan plane hit distance is scene scale");

    const Vec3 position = vecEvalRay(rayOrigin, rayDir, hit.t);
    const Vec3 wi = vecMake3(0.2f, 0.1f, 0.97f);
    const Vec3 shadingNormal = hit.normal;

    expectTrue(
        !lightIsOccluded(position, shadingNormal, wi, sceneGpu, hit.t, hit.triangleIndex),
        "environment shadow ray does not self-occlude on dense fan plane");

    const Vec3 grazingWi = vecNormalize3(vecMake3(0.98f, 0.02f, 0.18f));
    expectTrue(
        !lightIsOccluded(position, shadingNormal, grazingWi, sceneGpu, hit.t, hit.triangleIndex),
        "grazing environment shadow ray does not self-occlude on dense fan plane");
}

void testGeometricNormalFallbackOnSliverTriangle()
{
    TriangleGpu tri{};
    tri.v0 = vecMake3(0.0f, 0.0f, 0.0f);
    tri.v1 = vecMake3(1000.0f, 0.0f, 0.0f);
    tri.v2 = vecMake3(1000.0f, 0.001f, 0.0f);
    tri.n0 = vecMake3(0.0f, 0.0f, -1.0f);
    tri.n1 = vecMake3(0.0f, 0.0f, -1.0f);
    tri.n2 = vecMake3(0.0f, 0.0f, -1.0f);

    const Vec3 ro = vecMake3(500.0f, 0.0002f, 1.0f);
    const Vec3 rd = vecMake3(0.0f, 0.0f, -1.0f);
    float tHit = 0.0f;
    Vec3 normal{};
    Vec2 uv{};
    expectTrue(
        meshAccelRayTriangle(ro, rd, tri, 0.0f, 1.0e6f, tHit, normal, uv),
        "sliver triangle intersection succeeds");

    expectTrue(vecDot3(normal, vecMake3(0.0f, 0.0f, 1.0f)) > 0.9f, "sliver triangle uses geometric normal fallback");
}

} // namespace

int main()
{
    testRayEpsilonScalesWithHitDistanceAndExtent();
    testGroundQuadTraceAndShadow();
    testFanPlaneEnvironmentShadowRaysDoNotSelfOcclude();
    testGeometricNormalFallbackOnSliverTriangle();

    if (gFailures != 0) {
        std::cerr << gFailures << " test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "All shadow epsilon tests passed\n";
    return EXIT_SUCCESS;
}
