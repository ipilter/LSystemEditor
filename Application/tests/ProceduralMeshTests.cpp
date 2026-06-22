#include "Procedural/ProceduralMeshBuilder.h"
#include "Procedural/ProceduralSceneMeshBuilder.h"
#include "Procedural/Spline.h"
#include "Procedural/Turtle.h"
#include "Geometry/MathCore.h"
#include "LSystemEvaluator.h"
#include "MeshAccel/Mesh.h"
#include "MeshAccel/MeshAccelScene.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "MeshSmoothNormals.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern int gFailures;

namespace {

void expectTrue(bool condition, const char* label)
{
    if (!condition) {
        ++gFailures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

void expectEqSize(size_t actual, size_t expected, const char* label)
{
    if (actual != expected) {
        ++gFailures;
        std::cerr << "FAIL: " << label << " expected size " << expected << " got " << actual << '\n';
    }
}

void expectGeSize(size_t actual, size_t minimum, const char* label)
{
    if (actual < minimum) {
        ++gFailures;
        std::cerr << "FAIL: " << label << " expected at least " << minimum << " got " << actual << '\n';
    }
}

void expectNear(float actual, float expected, float tolerance, const char* label)
{
    if (std::fabs(actual - expected) > tolerance) {
        ++gFailures;
        std::cerr << "FAIL: " << label << " expected " << expected << " got " << actual << '\n';
    }
}

TurtleOutput turtleFromDefinition(const char* definition, const TurtleParams& params, std::size_t iterations = 0)
{
    LSystemEvaluationResult result = LSystemEvaluator::evaluate(definition, iterations);
    return turtleExecute(result.generation, params);
}

size_t loftFrameCountFromDefinition(
    const char* definition,
    const TurtleParams& params,
    int samplesPerSpan = 4,
    std::size_t iterations = 0)
{
    const TurtleOutput output = turtleFromDefinition(definition, params, iterations);
    if (output.segments.empty()) {
        return 0;
    }

    SplinePath path;
    if (!path.buildFromSegment(output.segments.front())) {
        return 0;
    }

    return path.computeLoftFrames(samplesPerSpan).size();
}

size_t loftFrameCountForSegment(const TurtleSegment& segment, int samplesPerSpan = 4)
{
    SplinePath path;
    if (!path.buildFromSegment(segment)) {
        return 0;
    }
    return path.computeLoftFrames(samplesPerSpan).size();
}

float distancePointToSegment(Vec3 point, Vec3 segmentStart, Vec3 segmentEnd)
{
    const Vec3 edge = vecSub3(segmentEnd, segmentStart);
    const float edgeLengthSquared = vecDot3(edge, edge);
    if (edgeLengthSquared <= 1e-12f) {
        return vecLength3(vecSub3(point, segmentStart));
    }

    const float t = vecClamp(vecDot3(vecSub3(point, segmentStart), edge) / edgeLengthSquared, 0.0f, 1.0f);
    const Vec3 closest = vecAdd3(segmentStart, vecScale3(edge, t));
    return vecLength3(vecSub3(point, closest));
}

bool meshHasInteriorAxisTriangles(
    const Mesh& mesh,
    Vec3 axisStart,
    Vec3 axisEnd,
    float expectedRadius,
    float axisThresholdFraction = 0.05f)
{
    const float threshold = expectedRadius * axisThresholdFraction;
    for (const MeshTriangle& triangle : mesh.triangles) {
        const float maxDistance = vecMax3(
            distancePointToSegment(triangle.v0, axisStart, axisEnd),
            distancePointToSegment(triangle.v1, axisStart, axisEnd),
            distancePointToSegment(triangle.v2, axisStart, axisEnd));
        if (maxDistance < threshold) {
            return true;
        }
    }
    return false;
}

float meshMaxExtent(const Mesh& mesh)
{
    const MeshAabb aabb = meshComputeAabb(mesh);
    return vecMax3(
        aabb.max.x - aabb.min.x,
        aabb.max.y - aabb.min.y,
        aabb.max.z - aabb.min.z);
}

void expectSphereDiameter(const Mesh& mesh, float expectedDiameter, float tolerance, const char* label)
{
    expectNear(meshMaxExtent(mesh), expectedDiameter, tolerance, label);
}

Vec3 triangleCentroid(const TriangleGpu& tri)
{
    return vecScale3(vecAdd3(vecAdd3(tri.v0, tri.v1), tri.v2), 1.0f / 3.0f);
}

bool normalNear(Vec3 actual, Vec3 expected, float tolerance)
{
    return vecLength3(vecSub3(actual, expected)) <= tolerance;
}

Vec3 interpolatedTriangleNormal(const TriangleGpu& tri, float u, float v)
{
    return vecNormalize3(vecAdd3(
        vecAdd3(vecScale3(tri.n0, 1.0f - u - v), vecScale3(tri.n1, u)),
        vecScale3(tri.n2, v)));
}

Vec3 centroidInterpolatedNormal(const TriangleGpu& tri)
{
    return interpolatedTriangleNormal(tri, 1.0f / 3.0f, 1.0f / 3.0f);
}

bool cornerNormalsEqual(const TriangleGpu& tri, float tolerance)
{
    return normalNear(tri.n0, tri.n1, tolerance) && normalNear(tri.n1, tri.n2, tolerance);
}

Vec3 horizontalRadialNormal(Vec3 centroid)
{
    const float length = vecLength3(vecMake3(centroid.x, centroid.y, 0.0f));
    if (length <= 1e-6f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }
    return vecScale3(vecMake3(centroid.x, centroid.y, 0.0f), 1.0f / length);
}

struct TrianglePositionKey
{
    int64_t v0 = 0;
    int64_t v1 = 0;
    int64_t v2 = 0;

    bool operator==(const TrianglePositionKey& other) const
    {
        return v0 == other.v0 && v1 == other.v1 && v2 == other.v2;
    }
};

struct TrianglePositionKeyHash
{
    size_t operator()(const TrianglePositionKey& key) const
    {
        const size_t h0 = static_cast<size_t>(key.v0) * 73856093u;
        const size_t h1 = static_cast<size_t>(key.v1) * 19349663u;
        const size_t h2 = static_cast<size_t>(key.v2) * 83492791u;
        return h0 ^ h1 ^ h2;
    }
};

int64_t quantizeComponent(float value, float cellSize)
{
    return static_cast<int64_t>(std::floor(value / cellSize + 0.5f));
}

int64_t quantizeVec3Key(Vec3 position, float cellSize)
{
    const int64_t x = quantizeComponent(position.x, cellSize);
    const int64_t y = quantizeComponent(position.y, cellSize);
    const int64_t z = quantizeComponent(position.z, cellSize);
    return x ^ (y << 21) ^ (z << 42);
}

TrianglePositionKey sortedTrianglePositionKey(Vec3 a, Vec3 b, Vec3 c, float cellSize)
{
    int64_t keys[3] = {
        quantizeVec3Key(a, cellSize),
        quantizeVec3Key(b, cellSize),
        quantizeVec3Key(c, cellSize),
    };

    if (keys[0] > keys[1]) {
        std::swap(keys[0], keys[1]);
    }
    if (keys[1] > keys[2]) {
        std::swap(keys[1], keys[2]);
    }
    if (keys[0] > keys[1]) {
        std::swap(keys[0], keys[1]);
    }

    return TrianglePositionKey{keys[0], keys[1], keys[2]};
}

bool meshHasDuplicatePositionTriangles(const Mesh& mesh, float cellSize = 0.01f)
{
    std::unordered_map<TrianglePositionKey, int, TrianglePositionKeyHash> seen;
    seen.reserve(mesh.triangles.size());
    for (const MeshTriangle& tri : mesh.triangles) {
        const TrianglePositionKey key = sortedTrianglePositionKey(tri.v0, tri.v1, tri.v2, cellSize);
        if (seen.find(key) != seen.end()) {
            return true;
        }
        seen.emplace(key, 1);
    }
    return false;
}

bool meshAllTriangleShadingNormalsValid(const MeshAccelScene& scene, float minLength = 0.9f)
{
    for (const TriangleGpu& tri : scene.trianglesHost()) {
        if (vecLength3(centroidInterpolatedNormal(tri)) < minLength) {
            return false;
        }
    }
    return true;
}

float meshMaxSideEdgeLength(const Mesh& mesh, float axisCenterThreshold = 1.0f)
{
    float maxLength = 0.0f;
    for (const MeshTriangle& tri : mesh.triangles) {
        const auto radialDistance = [](Vec3 position) {
            return vecLength3(vecMake3(position.x, position.y, 0.0f));
        };

        if (radialDistance(tri.v0) <= axisCenterThreshold || radialDistance(tri.v1) <= axisCenterThreshold
            || radialDistance(tri.v2) <= axisCenterThreshold) {
            continue;
        }

        maxLength = std::max(maxLength, vecLength3(vecSub3(tri.v1, tri.v0)));
        maxLength = std::max(maxLength, vecLength3(vecSub3(tri.v2, tri.v1)));
        maxLength = std::max(maxLength, vecLength3(vecSub3(tri.v0, tri.v2)));
    }
    return maxLength;
}

bool meshHasOutwardSideNormalsOnTaper(
    const MeshAccelScene& scene,
    float zMin,
    float zMax,
    float minRadial,
    float minOutwardDot = 0.2f)
{
    for (const TriangleGpu& tri : scene.trianglesHost()) {
        const Vec3 centroid = triangleCentroid(tri);
        if (centroid.z < zMin || centroid.z > zMax) {
            continue;
        }

        const Vec3 axisPoint = vecMake3(0.0f, 0.0f, centroid.z);
        const Vec3 outward = vecNormalize3(vecSub3(centroid, axisPoint));
        if (vecLength3(outward) < 0.5f || vecLength3(vecMake3(centroid.x, centroid.y, 0.0f)) < minRadial) {
            continue;
        }

        const Vec3 normal = centroidInterpolatedNormal(tri);
        if (vecDot3(normal, outward) >= minOutwardDot) {
            return true;
        }
    }
    return false;
}

bool meshHasShoulderNormalSplit(const MeshAccelScene& scene, float shoulderZ, float creaseAngleDeg)
{
    const float cosThreshold = std::cos(creaseAngleDeg * 3.14159265f / 180.0f);
    std::vector<Vec3> upperNormals;
    std::vector<Vec3> lowerNormals;

    for (const TriangleGpu& tri : scene.trianglesHost()) {
        const Vec3 centroid = triangleCentroid(tri);
        const float radial = vecLength3(vecMake3(centroid.x, centroid.y, 0.0f));
        if (radial < 50.0f) {
            continue;
        }

        const Vec3 normal = centroidInterpolatedNormal(tri);
        if (centroid.z > shoulderZ && centroid.z < shoulderZ + 50.0f) {
            upperNormals.push_back(normal);
        }
        if (centroid.z < shoulderZ && centroid.z > shoulderZ - 80.0f) {
            lowerNormals.push_back(normal);
        }
    }

    if (upperNormals.empty() || lowerNormals.empty()) {
        return false;
    }

    for (const Vec3& upperNormal : upperNormals) {
        for (const Vec3& lowerNormal : lowerNormals) {
            if (vecDot3(upperNormal, lowerNormal) < cosThreshold) {
                return true;
            }
        }
    }
    return false;
}

void expectPearlSpindleMesh(const char* definition, const char* labelPrefix, size_t minLoftFrames)
{
    ProceduralBuildParams buildParams{};
    buildParams.creaseAngleDeg = 50.0f;
    buildParams.circularSegments = 32;

    Mesh mesh{};
    expectTrue(
        ProceduralMeshBuilder::buildHostMesh(definition, 0, RootTransform{}, mesh, buildParams),
        (std::string(labelPrefix) + " build succeeds").c_str());

    expectTrue(!mesh.triangles.empty(), (std::string(labelPrefix) + " mesh has triangles").c_str());
    expectTrue(
        !meshHasDuplicatePositionTriangles(mesh),
        (std::string(labelPrefix) + " mesh has no duplicate position triangles").c_str());
    expectTrue(
        meshMaxSideEdgeLength(mesh) <= 55.0f,
        (std::string(labelPrefix) + " mesh side edges stay within ring spacing").c_str());

    const size_t loftFrames = loftFrameCountFromDefinition(definition, buildParams.turtle, 4, 0);
    expectGeSize(loftFrames, minLoftFrames, (std::string(labelPrefix) + " loft uses radius refinement rings").c_str());

    MeshAccelScene scene;
    expectTrue(scene.build(mesh), (std::string(labelPrefix) + " mesh accel scene builds").c_str());
    expectTrue(
        meshAllTriangleShadingNormalsValid(scene),
        (std::string(labelPrefix) + " mesh has valid shading normals").c_str());
    expectTrue(
        meshHasOutwardSideNormalsOnTaper(scene, -290.0f, -110.0f, 15.0f, 0.05f),
        (std::string(labelPrefix) + " steep taper uses outward-facing normals").c_str());
}

void expectPearlSpindleShoulderCrease(const char* definition, float shoulderZ)
{
    ProceduralBuildParams buildParams{};
    buildParams.creaseAngleDeg = 50.0f;
    buildParams.circularSegments = 32;

    Mesh mesh{};
    expectTrue(
        ProceduralMeshBuilder::buildHostMesh(definition, 0, RootTransform{}, mesh, buildParams),
        "pearl spindle shoulder crease build succeeds");

    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "pearl spindle shoulder crease scene builds");
    expectTrue(
        meshHasShoulderNormalSplit(scene, shoulderZ, buildParams.creaseAngleDeg),
        "pearl spindle shoulder keeps hard crease split normals");
}

void expectSingleFCylinderOutwardNormals()
{
    ProceduralBuildParams buildParams{};
    buildParams.creaseAngleDeg = 50.0f;

    Mesh mesh{};
    expectTrue(
        ProceduralMeshBuilder::buildHostMesh("F\n", 0, RootTransform{}, mesh, buildParams),
        "single F normal test build succeeds");

    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "single F normal test mesh accel scene builds");

    bool foundStartCap = false;
    bool foundEndCap = false;
    bool foundSmoothSide = false;
    bool foundFlatCapNormals = false;
    bool foundSmoothSideCornerNormals = false;

    constexpr float normalTolerance = 0.15f;
    constexpr float capCentroidTolerance = 60.0f;
    constexpr float sideCentroidTolerance = 40.0f;
    constexpr float radialDotMin = 0.85f;
    constexpr float cornerNormalTolerance = 0.05f;

    for (const TriangleGpu& tri : scene.trianglesHost()) {
        const Vec3 centroid = triangleCentroid(tri);
        const Vec3 normal = centroidInterpolatedNormal(tri);

        if (centroid.z > -capCentroidTolerance && std::fabs(centroid.x) < 150.0f && std::fabs(centroid.y) < 150.0f) {
            if (normalNear(normal, vecMake3(0.0f, 0.0f, 1.0f), normalTolerance)) {
                foundStartCap = true;
            }
            if (cornerNormalsEqual(tri, cornerNormalTolerance)) {
                foundFlatCapNormals = true;
            }
        }

        if (std::fabs(centroid.z + 500.0f) < capCentroidTolerance && std::fabs(centroid.x) < 150.0f
            && std::fabs(centroid.y) < 150.0f) {
            if (normalNear(normal, vecMake3(0.0f, 0.0f, -1.0f), normalTolerance)) {
                foundEndCap = true;
            }
            if (cornerNormalsEqual(tri, cornerNormalTolerance)) {
                foundFlatCapNormals = true;
            }
        }

        const Vec3 radial = horizontalRadialNormal(centroid);
        if (vecLength3(radial) > 0.5f && centroid.z <= 0.0f && centroid.z >= -500.0f) {
            if (vecDot3(normal, radial) > radialDotMin) {
                foundSmoothSide = true;
            }
            if (!cornerNormalsEqual(tri, cornerNormalTolerance)) {
                foundSmoothSideCornerNormals = true;
            }
        }
    }

    expectTrue(foundStartCap, "single F start cap normal points +Z");
    expectTrue(foundEndCap, "single F end cap normal points -Z");
    expectTrue(foundSmoothSide, "single F side normal aligns with radial direction");
    expectTrue(foundFlatCapNormals, "single F cap triangles use flat corner normals");
    expectTrue(foundSmoothSideCornerNormals, "single F side triangles use smooth corner normals");
}

Vec3 orientedTriangleNormal(const TriangleGpu& tri, Vec3 rd)
{
    Vec3 n = centroidInterpolatedNormal(tri);
    if (vecDot3(n, rd) > 0.0f) {
        n = vecScale3(n, -1.0f);
    }
    return n;
}

void expectSphereRadialNormals(const MeshAccelScene& scene, const char* labelPrefix)
{
    constexpr float radialDotMin = 0.85f;
    constexpr float axisNormalTolerance = 0.2f;
    constexpr float axisCentroidMin = 40.0f;
    constexpr float axisCrossMax = 30.0f;

    bool foundPosX = false;
    bool foundNegX = false;
    bool foundPosY = false;
    bool foundNegY = false;
    bool foundPosZ = false;
    bool foundNegZ = false;
    bool allRadialAligned = true;
    bool foundSmoothEquatorCorners = false;

    constexpr float cornerNormalTolerance = 0.05f;
    constexpr float equatorCentroidTolerance = 20.0f;

    for (const TriangleGpu& tri : scene.trianglesHost()) {
        const Vec3 centroid = triangleCentroid(tri);
        const float centroidLength = vecLength3(centroid);
        if (centroidLength <= 1e-6f) {
            continue;
        }

        const Vec3 radial = vecScale3(centroid, 1.0f / centroidLength);
        if (vecDot3(radial, centroidInterpolatedNormal(tri)) < radialDotMin) {
            allRadialAligned = false;
        }

        const Vec3 hitNormal = orientedTriangleNormal(tri, vecScale3(radial, -1.0f));
        const float ax = std::fabs(centroid.y) + std::fabs(centroid.z);
        const float ay = std::fabs(centroid.x) + std::fabs(centroid.z);
        const float az = std::fabs(centroid.x) + std::fabs(centroid.y);

        if (centroid.x > axisCentroidMin && ax < axisCrossMax) {
            if (normalNear(hitNormal, vecMake3(1.0f, 0.0f, 0.0f), axisNormalTolerance)) {
                foundPosX = true;
            }
        }
        if (centroid.x < -axisCentroidMin && ax < axisCrossMax) {
            if (normalNear(hitNormal, vecMake3(-1.0f, 0.0f, 0.0f), axisNormalTolerance)) {
                foundNegX = true;
            }
        }
        if (centroid.y > axisCentroidMin && ay < axisCrossMax) {
            if (normalNear(hitNormal, vecMake3(0.0f, 1.0f, 0.0f), axisNormalTolerance)) {
                foundPosY = true;
            }
        }
        if (centroid.y < -axisCentroidMin && ay < axisCrossMax) {
            if (normalNear(hitNormal, vecMake3(0.0f, -1.0f, 0.0f), axisNormalTolerance)) {
                foundNegY = true;
            }
        }
        if (centroid.z > axisCentroidMin && az < axisCrossMax) {
            if (normalNear(hitNormal, vecMake3(0.0f, 0.0f, 1.0f), axisNormalTolerance)) {
                foundPosZ = true;
            }
        }
        if (centroid.z < -axisCentroidMin && az < axisCrossMax) {
            if (normalNear(hitNormal, vecMake3(0.0f, 0.0f, -1.0f), axisNormalTolerance)) {
                foundNegZ = true;
            }
        }

        if (std::fabs(centroid.y) < equatorCentroidTolerance && centroidLength > 50.0f) {
            if (!cornerNormalsEqual(tri, cornerNormalTolerance)) {
                foundSmoothEquatorCorners = true;
            }
        }
    }

    expectTrue(allRadialAligned, labelPrefix);
    expectTrue(foundPosX, "sphere +X patch normal points +X");
    expectTrue(foundNegX, "sphere -X patch normal points -X");
    expectTrue(foundPosY, "sphere +Y patch normal points +Y");
    expectTrue(foundNegY, "sphere -Y patch normal points -Y");
    expectTrue(foundPosZ, "sphere +Z patch normal points +Z");
    expectTrue(foundNegZ, "sphere -Z patch normal points -Z");
    expectTrue(foundSmoothEquatorCorners, "sphere equator uses smooth corner normals");
}

void expectMeshAssignSmoothNormalsOnCoarseFan()
{
    Mesh mesh{};
    MeshTriangle left{};
    left.v0 = vecMake3(0.0f, 0.0f, 0.0f);
    left.v1 = vecMake3(1.0f, 0.0f, 0.0f);
    left.v2 = vecMake3(1.0f, 1.0f, 0.0f);
    const Vec3 faceNormal = vecMake3(0.0f, 0.0f, 1.0f);
    left.n0 = faceNormal;
    left.n1 = faceNormal;
    left.n2 = faceNormal;

    MeshTriangle right{};
    right.v0 = vecMake3(0.0f, 0.0f, 0.0f);
    right.v1 = vecMake3(1.0f, 1.0f, 0.0f);
    right.v2 = vecMake3(0.0f, 1.0f, 0.0f);
    right.n0 = faceNormal;
    right.n1 = faceNormal;
    right.n2 = faceNormal;

    mesh.triangles.push_back(left);
    mesh.triangles.push_back(right);

    meshAssignSmoothNormals(mesh, 60.0f);

    expectTrue(
        normalNear(mesh.triangles[0].n0, mesh.triangles[1].n0, 0.01f),
        "smooth normal pass welds shared corner at quad origin");
    expectTrue(
        normalNear(mesh.triangles[0].n2, mesh.triangles[1].n1, 0.01f),
        "smooth normal pass welds shared corner at quad diagonal");
}

void expectF0SphereOutwardNormals()
{
    Mesh mesh{};
    expectTrue(
        ProceduralMeshBuilder::buildHostMesh("F(0)\n", 0, RootTransform{}, mesh),
        "F(0) sphere normal test build succeeds");

    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "F(0) sphere normal test mesh accel scene builds");
    expectSphereRadialNormals(scene, "F(0) sphere radial normals align with centroid");
}

} // namespace

void runProceduralMeshTests()
{
    TurtleParams params{};
    params.defaultStepLength = 500.0f;
    params.defaultRadius = 100.0f;

    {
        const TurtleOutput output = turtleFromDefinition("F\n", params);
        expectEqSize(output.segments.size(), 1, "single F produces one segment");
        if (!output.segments.empty()) {
            expectEqSize(output.segments.front().states.size(), 2, "single F segment has start and end states");
        }
    }

    {
        Mesh mesh{};
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh("F\n", 0, RootTransform{}, mesh),
            "procedural build succeeds for single F axiom");
        expectTrue(!mesh.triangles.empty(), "single F procedural mesh has triangles");
    }

    {
        expectSingleFCylinderOutwardNormals();
    }

    {
        expectF0SphereOutwardNormals();
    }

    {
        expectMeshAssignSmoothNormalsOnCoarseFan();
    }

    {
        expectEqSize(loftFrameCountFromDefinition("F\n", params), 2, "single F loft uses two knot rings");
        expectEqSize(loftFrameCountFromDefinition("F F\n", params), 3, "F F loft uses three knot rings");
    }

    {
        constexpr const char* pearlSpindle3 =
            "[F(100, 10)\n"
            "F(200, 100)\n"
            "F(100, 10)]";
        expectPearlSpindleMesh(pearlSpindle3, "pearl spindle three-span", 15);
        expectPearlSpindleShoulderCrease(pearlSpindle3, -100.0f);
    }

    {
        constexpr const char* pearlSpindle2 =
            "[F(100, 10)\n"
            "F(200, 100)]";
        expectPearlSpindleMesh(pearlSpindle2, "pearl spindle two-span", 10);
    }

    {
        Mesh mesh{};
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh("F(2, 1000, 1000)\n", 0, RootTransform{}, mesh),
            "procedural build succeeds for F(2, 1000, 1000)");
        expectTrue(!mesh.triangles.empty(), "F(2, 1000, 1000) mesh has triangles");
        expectTrue(
            !meshHasInteriorAxisTriangles(
                mesh,
                Vec3{0.0f, 0.0f, 0.0f},
                Vec3{0.0f, 0.0f, -2000.0f},
                1000.0f),
            "F(2, 1000, 1000) mesh has no interior axis-near rogue triangles");
    }

    {
        Mesh mesh{};
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh("F(0, 1000)\n", 0, RootTransform{}, mesh),
            "procedural build succeeds for F(0, 1000)");
        expectTrue(!mesh.triangles.empty(), "F(0, 1000) mesh has triangles");
        expectSphereDiameter(mesh, 2000.0f, 80.0f, "F(0, 1000) mesh sphere diameter");
    }

    {
        Mesh mesh{};
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh("F(0, 1000, 1000)\n", 0, RootTransform{}, mesh),
            "procedural build succeeds for F(0, 1000, 1000)");
        expectTrue(!mesh.triangles.empty(), "F(0, 1000, 1000) mesh has triangles");
        expectSphereDiameter(mesh, 2000.0f, 80.0f, "F(0, 1000, 1000) mesh sphere diameter");
    }

    {
        Mesh mesh{};
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh("F(0)\n", 0, RootTransform{}, mesh),
            "procedural build succeeds for F(0)");
        expectTrue(!mesh.triangles.empty(), "F(0) mesh has triangles");
        expectSphereDiameter(mesh, 2.0f * params.defaultRadius, 80.0f, "F(0) mesh uses defaultRadius sphere");
    }

    {
        const TurtleOutput output = turtleFromDefinition("F f F\n", params);
        expectEqSize(output.segments.size(), 2, "F f F produces two segments for loft");
        if (output.segments.size() == 2) {
            expectEqSize(loftFrameCountForSegment(output.segments[0]), 2, "first F f F segment has two rings");
            expectEqSize(loftFrameCountForSegment(output.segments[1]), 2, "second F f F segment has two rings");
        }
    }

    {
        const TurtleOutput output = turtleFromDefinition("F [Pitch(45) F]\n", params);
        expectEqSize(output.segments.size(), 2, "F [Pitch(45) F] produces two loft segments");
    }

    {
        const TurtleOutput output = turtleFromDefinition("F F F\n", params);
        expectEqSize(output.segments.size(), 1, "F F F produces one segment");
        if (!output.segments.empty()) {
            expectEqSize(output.segments.front().states.size(), 4, "F F F segment has four states");
        }
    }

    {
        const TurtleOutput output = turtleFromDefinition("F ] F ] F\n", params);
        expectEqSize(output.segments.size(), 3, "F ] F ] F produces three segments");
    }

    {
        const TurtleOutput output = turtleFromDefinition("F f F\n", params);
        expectEqSize(output.segments.size(), 2, "F f F produces two segments");
        if (output.segments.size() == 2) {
            const Vec3& endPosition = output.segments.back().states.back().position;
            expectNear(endPosition.z, -1500.0f, 1e-2f, "F f F ends after two drawn and one pen-up step");
        }
    }

    {
        const TurtleOutput output = turtleFromDefinition("F [ [ F F ] [ F F ] ] F\n", params);
        expectGeSize(output.segments.size(), 2, "branch example creates multiple segments");
        bool hasBranchGroup = false;
        for (const BranchGroup& group : output.branchGroups) {
            if (group.segmentIndices.size() > 1) {
                hasBranchGroup = true;
            }
        }
        expectTrue(hasBranchGroup, "branch with multiple segments creates BranchGroup");
    }

    {
        TurtleSegment segment{};
        segment.states = {
            TurtleState{Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f}, 200.0f},
            TurtleState{Vec3{0.0f, 0.0f, -1000.0f}, Vec3{0.0f, 0.0f, -1.0f}, 100.0f}};
        SplinePath path;
        expectTrue(path.buildFromSegment(segment), "spline builds from segment");
        const std::vector<SplineSample> samples = path.sampleUniform(4);
        expectGeSize(samples.size(), 2, "spline samples generated");
        if (samples.size() >= 2) {
            const float startRadius = samples.front().radius;
            const float endRadius = samples.back().radius;
            expectTrue(startRadius >= endRadius, "radius decreases along simple segment");
        }
    }

    {
        Mesh mesh{};
        const std::string definition =
            "F(800) Yaw(2) F(500) Yaw(1) F(300) Yaw(3) [Pitch(45) F F F][Pitch(-45) F F F]";
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh(definition, 0, RootTransform{}, mesh),
            "procedural build succeeds");
        expectTrue(!mesh.triangles.empty(), "procedural mesh has triangles");

        MeshAccelScene scene;
        expectTrue(scene.build(mesh), "mesh accel scene builds from procedural mesh");
        expectTrue(!scene.trianglesHost().empty(), "procedural mesh triangle count positive");
    }

    {
        std::vector<ProceduralInstance> instances;
        ProceduralInstance instance{};
        instance.commandString = "F F F";
        instance.iterations = 0;
        instances.push_back(std::move(instance));

        Mesh mesh{};
        MeshAccelScene scene;
        expectTrue(buildMeshFromInstances(instances, ProceduralBuildParams{}, mesh), "buildMeshFromInstances with procedural instance");
        expectTrue(scene.build(mesh), "mesh accel scene builds from procedural instance mesh");
    }

    {
        Mesh mesh{};
        ProceduralBuildParams buildParams{};
        std::string error;
        const bool ok = ProceduralMeshBuilder::buildHostMesh(
            "A -> B\nnot a rule", 0, RootTransform{}, mesh, buildParams, &error);
        expectTrue(!ok, "invalid L-system returns false");
        expectTrue(!error.empty(), "invalid L-system sets error message");
    }
}
