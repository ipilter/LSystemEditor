#include "Loft.h"
#include "Geometry/MathCore.h"
#include "ManifoldMeshConvert.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "ProceduralTypes.h"
#include "Turtle.h"

#include <algorithm>
#include <cmath>
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

struct UvBounds
{
    float minU = 1.0f;
    float maxU = 0.0f;
    float minV = 1.0f;
    float maxV = 0.0f;
};

void includeUv(UvBounds& bounds, const Vec2& uv)
{
    bounds.minU = std::min(bounds.minU, uv.x);
    bounds.maxU = std::max(bounds.maxU, uv.x);
    bounds.minV = std::min(bounds.minV, uv.y);
    bounds.maxV = std::max(bounds.maxV, uv.y);
}

UvBounds triangleUvBounds(const HostTriangle& triangle)
{
    UvBounds bounds{};
    includeUv(bounds, triangle.uv0);
    includeUv(bounds, triangle.uv1);
    includeUv(bounds, triangle.uv2);
    return bounds;
}

bool isCapFacingTriangle(const HostTriangle& triangle, const Vec3& axis)
{
    const Vec3 edge0 = vecSub3(triangle.v1, triangle.v0);
    const Vec3 edge1 = vecSub3(triangle.v2, triangle.v0);
    const Vec3 normal = vecNormalize3(vecCross3(edge0, edge1));
    return std::fabs(vecDot3(normal, axis)) > 0.9f;
}

TurtleSegment makeShortWideLoftSegment()
{
    TurtleSegment segment{};
    TurtleState state{};
    state.position = Vec3{0.0f, 0.0f, 0.0f};
    state.tangent = Vec3{1.0f, 0.0f, 0.0f};
    state.radius = 2.0f;
    segment.states.push_back(state);

    state.position = Vec3{0.1f, 0.0f, 0.0f};
    segment.states.push_back(state);
    return segment;
}

HostMesh buildShortWideLoftHostMesh(const ProceduralBuildParams& params)
{
    return loftHostMeshFromSegment(makeShortWideLoftSegment(), params);
}

void testLoftMeshPreservesNonZeroUvs()
{
    TurtleSegment segment{};
    TurtleState state{};
    state.position = Vec3{0.0f, 0.0f, 0.0f};
    state.tangent = Vec3{0.0f, 1.0f, 0.0f};
    state.radius = 0.2f;
    segment.states.push_back(state);

    state.position = Vec3{0.0f, 1.0f, 0.0f};
    segment.states.push_back(state);

    ProceduralBuildParams params{};
    params.circularSegments = 8;
    params.samplesPerSpan = 4;

    const manifold::Manifold loftMesh = loftOrSphereFromSegment(segment, params);
    expectTrue(loftMesh.NumTri() > 0, "loft validation mesh has triangles");

    const HostMesh hostMesh = loftHostMeshFromSegment(segment, params);
    expectTrue(!hostMesh.triangles.empty(), "loft render mesh has triangles");

    bool foundNonZeroU = false;
    bool foundNonZeroV = false;
    for (const HostTriangle& triangle : hostMesh.triangles) {
        if (triangle.uv0.x > 0.0f || triangle.uv1.x > 0.0f || triangle.uv2.x > 0.0f) {
            foundNonZeroU = true;
        }
        if (triangle.uv0.y > 0.0f || triangle.uv1.y > 0.0f || triangle.uv2.y > 0.0f) {
            foundNonZeroV = true;
        }
    }

    expectTrue(foundNonZeroU, "loft mesh has non-zero U coordinates");
    expectTrue(foundNonZeroV, "loft mesh has non-zero V coordinates");
}

void testLoftCapDiscHasPlanarUvVariation()
{
    ProceduralBuildParams params{};
    params.circularSegments = 16;
    params.samplesPerSpan = 4;
    params.segmentRefineTolerance = 1.0f;

    const HostMesh hostMesh = buildShortWideLoftHostMesh(params);
    expectTrue(!hostMesh.triangles.empty(), "short wide loft has triangles");

    const Vec3 axis{1.0f, 0.0f, 0.0f};
    bool foundCapTriangle = false;
    bool foundCapVaryingU = false;
    bool foundCapVaryingV = false;
    UvBounds capBounds{};

    for (const HostTriangle& triangle : hostMesh.triangles) {
        if (!isCapFacingTriangle(triangle, axis)) {
            continue;
        }

        foundCapTriangle = true;
        const UvBounds bounds = triangleUvBounds(triangle);
        includeUv(capBounds, Vec2{bounds.minU, bounds.minV});
        includeUv(capBounds, Vec2{bounds.maxU, bounds.maxV});
        const float uSpan = bounds.maxU - bounds.minU;
        const float vSpan = bounds.maxV - bounds.minV;
        if (uSpan > 0.05f) {
            foundCapVaryingU = true;
        }
        if (vSpan > 0.05f) {
            foundCapVaryingV = true;
        }
    }

    expectTrue(foundCapTriangle, "short wide loft has cap-facing triangles");
    expectTrue(foundCapVaryingU || capBounds.maxU - capBounds.minU > 0.05f, "loft cap varies in U across cap triangles");
    expectTrue(foundCapVaryingV || capBounds.maxV - capBounds.minV > 0.05f, "loft cap varies in V across cap triangles");
    expectTrue(capBounds.minU < 0.55f && capBounds.maxU > 0.45f, "loft cap UV spans disc center in U");
    expectTrue(capBounds.minV < 0.55f && capBounds.maxV > 0.45f, "loft cap UV spans disc center in V");
}

void testLoftSideWallHasCylindricalUvVariation()
{
    ProceduralBuildParams params{};
    params.circularSegments = 16;
    params.samplesPerSpan = 4;
    params.segmentRefineTolerance = 1.0f;

    const HostMesh hostMesh = buildShortWideLoftHostMesh(params);
    const Vec3 axis{1.0f, 0.0f, 0.0f};

    bool foundSideTriangle = false;
    bool foundSideVaryingU = false;
    bool foundSideVaryingV = false;

    for (const HostTriangle& triangle : hostMesh.triangles) {
        if (isCapFacingTriangle(triangle, axis)) {
            continue;
        }

        foundSideTriangle = true;
        const UvBounds bounds = triangleUvBounds(triangle);
        if (bounds.maxU - bounds.minU > 0.01f) {
            foundSideVaryingU = true;
        }
        if (bounds.maxV - bounds.minV > 0.01f) {
            foundSideVaryingV = true;
        }
    }

    expectTrue(foundSideTriangle, "short wide loft has side-wall triangles");
    expectTrue(foundSideVaryingU, "loft side wall varies in U");
    expectTrue(foundSideVaryingV, "loft side wall varies in V");
}

void testLoftSideWallUsesSeamColumn()
{
    ProceduralBuildParams params{};
    params.circularSegments = 16;
    params.samplesPerSpan = 4;

    const HostMesh hostMesh = buildShortWideLoftHostMesh(params);
    const Vec3 axis{1.0f, 0.0f, 0.0f};

    bool foundSideTriangle = false;
    bool foundUAtOne = false;
    for (const HostTriangle& triangle : hostMesh.triangles) {
        if (isCapFacingTriangle(triangle, axis)) {
            continue;
        }

        foundSideTriangle = true;
        const UvBounds bounds = triangleUvBounds(triangle);
        const float maxU = std::max({triangle.uv0.x, triangle.uv1.x, triangle.uv2.x});
        if (maxU > 0.99f) {
            foundUAtOne = true;
        }

        const bool wrapAroundSeam =
            bounds.minU < 0.05f && bounds.maxU > 0.85f && bounds.maxU < 0.99f;
        expectTrue(!wrapAroundSeam, "no side triangle spans u=0 to u~15/16 without seam column");
        expectTrue(bounds.maxU - bounds.minU <= 0.5f, "loft side triangle has bounded U span");
    }

    expectTrue(foundSideTriangle, "short wide loft has side-wall triangles for seam column test");
    expectTrue(foundUAtOne, "side wall includes seam column at u=1");
}

void testSphereMeshUsesSphericalUvFallback()
{
    TurtleSegment segment{};
    TurtleState state{};
    state.position = Vec3{0.0f, 0.0f, 0.0f};
    state.tangent = Vec3{0.0f, 1.0f, 0.0f};
    state.radius = 0.5f;
    segment.states.push_back(state);
    segment.states.push_back(state);

    ProceduralBuildParams params{};
    params.circularSegments = 12;

    const manifold::Manifold sphereMesh = loftOrSphereFromSegment(segment, params);
    const HostMesh hostMesh = meshFromManifold(sphereMesh);
    expectTrue(!hostMesh.triangles.empty(), "sphere mesh has triangles");

    bool foundVaryingU = false;
    bool foundVaryingV = false;
    for (const HostTriangle& triangle : hostMesh.triangles) {
        const UvBounds bounds = triangleUvBounds(triangle);
        if (bounds.maxU - bounds.minU > 0.01f) {
            foundVaryingU = true;
        }
        if (bounds.maxV - bounds.minV > 0.01f) {
            foundVaryingV = true;
        }
    }

    expectTrue(foundVaryingU, "sphere fallback UV varies in U");
    expectTrue(foundVaryingV, "sphere fallback UV varies in V");
}

void testInterpolatesUvAtHit()
{
    TriangleGpu triangle{};
    triangle.v0 = Vec3{0.0f, 0.0f, 0.0f};
    triangle.v1 = Vec3{1.0f, 0.0f, 0.0f};
    triangle.v2 = Vec3{0.0f, 1.0f, 0.0f};
    triangle.n0 = Vec3{0.0f, 0.0f, 1.0f};
    triangle.n1 = Vec3{0.0f, 0.0f, 1.0f};
    triangle.n2 = Vec3{0.0f, 0.0f, 1.0f};
    triangle.uv0 = Vec2{0.0f, 0.0f};
    triangle.uv1 = Vec2{1.0f, 0.0f};
    triangle.uv2 = Vec2{0.0f, 1.0f};

    float hitT = 0.0f;
    Vec3 normal{};
    Vec2 uv{};
    const Vec3 origin{0.25f, 0.25f, 1.0f};
    const Vec3 direction{0.0f, 0.0f, -1.0f};
    expectTrue(
        meshAccelRayTriangle(origin, direction, triangle, 0.0f, 10.0f, hitT, normal, uv),
        "ray hits triangle");
    expectNear(uv.x, 0.25f, 1e-5f, "interpolated uv.x");
    expectNear(uv.y, 0.25f, 1e-5f, "interpolated uv.y");
}

void testUvOverlayModeEnumValue()
{
    expectTrue(
        static_cast<int>(RenderViewOverlayMode::Uv) == 3,
        "Uv view mode enum value is 3");
    expectTrue(
        static_cast<int>(RenderViewOverlayMode::AdaptiveSampling) < static_cast<int>(RenderViewOverlayMode::Uv),
        "Uv follows AdaptiveSampling in view mode enum");
}

} // namespace

int main()
{
    testLoftMeshPreservesNonZeroUvs();
    testLoftCapDiscHasPlanarUvVariation();
    testLoftSideWallHasCylindricalUvVariation();
    testLoftSideWallUsesSeamColumn();
    testSphereMeshUsesSphericalUvFallback();
    testInterpolatesUvAtHit();
    testUvOverlayModeEnumValue();

    if (gFailures == 0) {
        std::cout << "All mesh UV tests passed.\n";
        return 0;
    }

    std::cerr << gFailures << " mesh UV test(s) failed.\n";
    return 1;
}
