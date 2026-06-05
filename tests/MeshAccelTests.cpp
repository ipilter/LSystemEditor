#include "MeshBuilder/ManifoldMeshBuilder.h"
#include "MeshBuilder/HostMesh.h"
#include "MeshAccel/MeshAccelScene.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "MeshAccel/MeshSceneContent.h"
#include "Geometry/MathCore.h"
#include "ScenePrimitive.h"

#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

int gFailures = 0;

namespace {

void expectTrue(bool condition, const char* label)
{
    if (!condition) {
        ++gFailures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

void expectNear(float actual, float expected, float epsilon, const char* label)
{
    if (std::fabs(actual - expected) > epsilon) {
        ++gFailures;
        std::cerr << "FAIL: " << label << " expected " << expected << " got " << actual << '\n';
    }
}

std::vector<std::unique_ptr<ScenePrimitive>> makeSinglePrimitive(std::unique_ptr<ScenePrimitive> primitive)
{
    std::vector<std::unique_ptr<ScenePrimitive>> primitives;
    primitives.push_back(std::move(primitive));
    return primitives;
}

struct PrimitiveAabb
{
    Vec3 min{};
    Vec3 max{};
};

PrimitiveAabb sphereBounds(Vec3 center, float radius)
{
    const Vec3 r = Vec3{radius, radius, radius};
    return PrimitiveAabb{
        Vec3{center.x - r.x, center.y - r.y, center.z - r.z},
        Vec3{center.x + r.x, center.y + r.y, center.z + r.z}};
}

PrimitiveAabb cylinderBounds(Vec3 center, Vec2 halfExtents)
{
    return PrimitiveAabb{
        Vec3{center.x - halfExtents.x, center.y - halfExtents.y, center.z - halfExtents.x},
        Vec3{center.x + halfExtents.x, center.y + halfExtents.y, center.z + halfExtents.x}};
}

PrimitiveAabb cappedConeBounds(Vec3 center, float halfHeight, float radiusBottom, float radiusTop)
{
    const float maxRadius = radiusBottom > radiusTop ? radiusBottom : radiusTop;
    return PrimitiveAabb{
        Vec3{center.x - maxRadius, center.y - halfHeight, center.z - maxRadius},
        Vec3{center.x + maxRadius, center.y + halfHeight, center.z + maxRadius}};
}

bool aabbContains(const HostMeshAabb& mesh, const PrimitiveAabb& expected, float tolerance)
{
    return mesh.min.x <= expected.min.x + tolerance && mesh.min.y <= expected.min.y + tolerance
        && mesh.min.z <= expected.min.z + tolerance && mesh.max.x >= expected.max.x - tolerance
        && mesh.max.y >= expected.max.y - tolerance && mesh.max.z >= expected.max.z - tolerance;
}

std::unique_ptr<ScenePrimitive> makeSphere(Vec3 center, float radius)
{
    auto p = std::make_unique<ScenePrimitive>();
    p->type = PrimitiveType::Sphere;
    p->center = center;
    p->radius = radius;
    return p;
}

std::unique_ptr<ScenePrimitive> makeCylinder(Vec3 center, Vec2 halfExtents)
{
    auto p = std::make_unique<ScenePrimitive>();
    p->type = PrimitiveType::Cylinder;
    p->center = center;
    p->halfExtents = halfExtents;
    return p;
}

std::unique_ptr<ScenePrimitive> makeCappedCone(
    Vec3 center,
    float halfHeight,
    float radiusBottom,
    float radiusTop)
{
    auto p = std::make_unique<ScenePrimitive>();
    p->type = PrimitiveType::CappedCone;
    p->center = center;
    p->halfHeight = halfHeight;
    p->radiusBottom = radiusBottom;
    p->radiusTop = radiusTop;
    return p;
}

void testGpuStructLayout()
{
    expectTrue(sizeof(TriangleGpu) == 48, "TriangleGpuSize");
    expectTrue(sizeof(MeshBvhNode) == 48, "MeshBvhNodeSize");
    expectTrue(sizeof(MeshAccelSceneGpu) == 32, "MeshAccelSceneGpuSize");
    expectTrue(offsetof(MeshBvhNode, leftIndex) == 32, "MeshBvhNodeLeftIndexOffset");
}

void testSphereMeshAabb()
{
    auto primitives = makeSinglePrimitive(makeSphere(Vec3{1.0f, 2.0f, 3.0f}, 0.5f));
    HostMesh mesh{};
    expectTrue(ManifoldMeshBuilder::buildSceneMesh(primitives, mesh), "SphereMeshBuild");

    const HostMeshAabb meshAabb = hostMeshComputeAabb(mesh);
    const PrimitiveAabb expected = sphereBounds(Vec3{1.0f, 2.0f, 3.0f}, 0.5f);
    expectTrue(aabbContains(meshAabb, expected, 0.05f), "SphereMeshAabb");
}

void testCylinderMeshAabb()
{
    auto primitives = makeSinglePrimitive(makeCylinder(Vec3{0.0f, 0.0f, 0.0f}, Vec2{0.75f, 1.0f}));
    HostMesh mesh{};
    expectTrue(ManifoldMeshBuilder::buildSceneMesh(primitives, mesh), "CylinderMeshBuild");

    const HostMeshAabb meshAabb = hostMeshComputeAabb(mesh);
    const PrimitiveAabb expected = cylinderBounds(Vec3{0.0f, 0.0f, 0.0f}, Vec2{0.75f, 1.0f});
    const float extentTolerance = 0.08f;
    const float ex = expected.max.x - expected.min.x;
    const float ey = expected.max.y - expected.min.y;
    const float ez = expected.max.z - expected.min.z;
    const float mx = meshAabb.max.x - meshAabb.min.x;
    const float my = meshAabb.max.y - meshAabb.min.y;
    const float mz = meshAabb.max.z - meshAabb.min.z;
    expectNear(mx, ex, extentTolerance, "CylinderMeshExtentX");
    expectNear(my, ey, extentTolerance, "CylinderMeshExtentY");
    expectNear(mz, ez, extentTolerance, "CylinderMeshExtentZ");
}

void testCappedConeMeshAabb()
{
    auto primitives = makeSinglePrimitive(makeCappedCone(Vec3{0.0f, 0.0f, 0.0f}, 1.0f, 0.5f, 0.25f));
    HostMesh mesh{};
    expectTrue(ManifoldMeshBuilder::buildSceneMesh(primitives, mesh), "CappedConeMeshBuild");

    const HostMeshAabb meshAabb = hostMeshComputeAabb(mesh);
    const PrimitiveAabb expected = cappedConeBounds(Vec3{0.0f, 0.0f, 0.0f}, 1.0f, 0.5f, 0.25f);
    const float extentTolerance = 0.08f;
    expectNear(meshAabb.max.x - meshAabb.min.x, expected.max.x - expected.min.x, extentTolerance, "ConeMeshExtentX");
    expectNear(meshAabb.max.y - meshAabb.min.y, expected.max.y - expected.min.y, extentTolerance, "ConeMeshExtentY");
    expectNear(meshAabb.max.z - meshAabb.min.z, expected.max.z - expected.min.z, extentTolerance, "ConeMeshExtentZ");
}

void testMeshSceneBuild()
{
    auto primitives = makeSinglePrimitive(makeSphere(Vec3{0.0f, 0.0f, 0.0f}, 1.0f));
    MeshAccelScene scene;
    expectTrue(meshSceneBuildFromPrimitives(primitives, scene), "MeshSceneBuild");
    expectTrue(scene.isBuilt(), "MeshSceneBuilt");
    expectTrue(!scene.trianglesHost().empty(), "MeshSceneTriangles");
    expectTrue(!scene.bvhNodesHost().empty(), "MeshSceneBvh");
}

void testSingleSphereTightMeshExtent()
{
    auto primitives = makeSinglePrimitive(makeSphere(Vec3{0.0f, 0.0f, 0.0f}, 0.5f));
    HostMesh mesh{};
    expectTrue(ManifoldMeshBuilder::buildSceneMesh(primitives, mesh), "SingleSphereTightMeshBuild");

    const HostMeshAabb meshAabb = hostMeshComputeAabb(mesh);
    const float extentX = meshAabb.max.x - meshAabb.min.x;
    const float extentY = meshAabb.max.y - meshAabb.min.y;
    const float extentZ = meshAabb.max.z - meshAabb.min.z;
    const float maxAllowedExtent = 1.2f;
    expectTrue(extentX < maxAllowedExtent, "SingleSphereTightExtentX");
    expectTrue(extentY < maxAllowedExtent, "SingleSphereTightExtentY");
    expectTrue(extentZ < maxAllowedExtent, "SingleSphereTightExtentZ");
}

void testTwoSphereSceneMesh()
{
    std::vector<std::unique_ptr<ScenePrimitive>> primitives;
    primitives.push_back(makeSphere(Vec3{-0.3f, 0.0f, 0.0f}, 0.5f));
    primitives.push_back(makeSphere(Vec3{0.3f, 0.0f, 0.0f}, 0.5f));

    HostMesh mesh{};
    expectTrue(ManifoldMeshBuilder::buildSceneMesh(primitives, mesh), "TwoSphereSceneMeshBuild");
    expectTrue(!mesh.triangles.empty(), "TwoSphereSceneMeshTriangles");

    const HostMeshAabb meshAabb = hostMeshComputeAabb(mesh);
    const PrimitiveAabb left = sphereBounds(Vec3{-0.3f, 0.0f, 0.0f}, 0.5f);
    const PrimitiveAabb right = sphereBounds(Vec3{0.3f, 0.0f, 0.0f}, 0.5f);
    const float tolerance = 0.08f;
    expectTrue(aabbContains(meshAabb, left, tolerance), "TwoSphereSceneMeshAabbLeft");
    expectTrue(aabbContains(meshAabb, right, tolerance), "TwoSphereSceneMeshAabbRight");

    const float extentX = meshAabb.max.x - meshAabb.min.x;
    const float maxAllowedExtentX = 1.85f;
    expectTrue(extentX < maxAllowedExtentX, "TwoSphereSceneTightExtentX");
}

Vec3 triangleCentroid(const TriangleGpu& tri)
{
    return vecScale3(vecAdd3(vecAdd3(tri.v0, tri.v1), tri.v2), 1.0f / 3.0f);
}

Vec3 orientedTriangleNormal(const TriangleGpu& tri, Vec3 rd)
{
    const Vec3 e1 = vecSub3(tri.v1, tri.v0);
    const Vec3 e2 = vecSub3(tri.v2, tri.v0);
    Vec3 n = vecNormalize3(vecMake3(
        e1.y * e2.z - e1.z * e2.y,
        e1.z * e2.x - e1.x * e2.z,
        e1.x * e2.y - e1.y * e2.x));
    if (vecDot3(n, rd) > 0.0f) {
        n = vecScale3(n, -1.0f);
    }
    return n;
}

void testPrimitiveSphereRadialNormals()
{
    auto primitives = makeSinglePrimitive(makeSphere(Vec3{0.0f, 0.0f, 0.0f}, 0.5f));
    HostMesh mesh{};
    expectTrue(ManifoldMeshBuilder::buildSceneMesh(primitives, mesh), "PrimitiveSphereNormalMeshBuild");

    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "PrimitiveSphereNormalSceneBuild");

    constexpr float radialDotMin = 0.85f;
    bool allRadialAligned = true;
    bool foundPosX = false;

    for (const TriangleGpu& tri : scene.trianglesHost()) {
        const Vec3 centroid = triangleCentroid(tri);
        const float centroidLength = vecLength3(centroid);
        if (centroidLength <= 1e-6f) {
            continue;
        }

        const Vec3 radial = vecScale3(centroid, 1.0f / centroidLength);
        if (vecDot3(radial, tri.normal) < radialDotMin) {
            allRadialAligned = false;
        }

        const Vec3 hitNormal = orientedTriangleNormal(tri, vecScale3(radial, -1.0f));
        if (centroid.x > 0.2f && std::fabs(centroid.y) < 0.05f && std::fabs(centroid.z) < 0.05f) {
            if (vecLength3(vecSub3(hitNormal, vecMake3(1.0f, 0.0f, 0.0f))) < 0.2f) {
                foundPosX = true;
            }
        }
    }

    expectTrue(allRadialAligned, "PrimitiveSphereRadialNormals");
    expectTrue(foundPosX, "PrimitiveSpherePosXNormal");
}

} // namespace

void runProceduralMeshTests();

int main()
{
    testGpuStructLayout();
    testSphereMeshAabb();
    testCylinderMeshAabb();
    testCappedConeMeshAabb();
    testSingleSphereTightMeshExtent();
    testTwoSphereSceneMesh();
    testPrimitiveSphereRadialNormals();
    testMeshSceneBuild();
    runProceduralMeshTests();

    if (gFailures != 0) {
        std::cerr << gFailures << " test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "All mesh accel tests passed\n";
    return EXIT_SUCCESS;
}
