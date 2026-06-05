#include "Procedural/ProceduralMeshBuilder.h"
#include "Procedural/Spline.h"
#include "Procedural/Turtle.h"
#include "Geometry/MathCore.h"
#include "LSystemEvaluator.h"
#include "MeshAccel/MeshAccelScene.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "MeshAccel/MeshSceneContent.h"
#include "MeshBuilder/HostMesh.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
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
    const HostMesh& mesh,
    Vec3 axisStart,
    Vec3 axisEnd,
    float expectedRadius,
    float axisThresholdFraction = 0.05f)
{
    const float threshold = expectedRadius * axisThresholdFraction;
    for (const HostTriangle& triangle : mesh.triangles) {
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

float meshMaxExtent(const HostMesh& mesh)
{
    const HostMeshAabb aabb = hostMeshComputeAabb(mesh);
    return vecMax3(
        aabb.max.x - aabb.min.x,
        aabb.max.y - aabb.min.y,
        aabb.max.z - aabb.min.z);
}

void expectSphereDiameter(const HostMesh& mesh, float expectedDiameter, float tolerance, const char* label)
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

void expectSingleFCylinderOutwardNormals()
{
    HostMesh mesh{};
    expectTrue(
        ProceduralMeshBuilder::buildHostMesh("F\n", 0, RootTransform{}, mesh),
        "single F normal test build succeeds");

    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "single F normal test mesh accel scene builds");

    bool foundStartCap = false;
    bool foundEndCap = false;
    bool foundSidePosY = false;
    bool foundSideNegX = false;

    constexpr float normalTolerance = 0.15f;
    constexpr float capCentroidTolerance = 0.06f;
    constexpr float sideCentroidTolerance = 0.04f;

    for (const TriangleGpu& tri : scene.trianglesHost()) {
        const Vec3 centroid = triangleCentroid(tri);
        const Vec3 normal = tri.normal;

        if (centroid.z > -capCentroidTolerance && std::fabs(centroid.x) < 0.15f && std::fabs(centroid.y) < 0.15f) {
            if (normalNear(normal, vecMake3(0.0f, 0.0f, 1.0f), normalTolerance)) {
                foundStartCap = true;
            }
        }

        if (std::fabs(centroid.z + 0.5f) < capCentroidTolerance && std::fabs(centroid.x) < 0.15f
            && std::fabs(centroid.y) < 0.15f) {
            if (normalNear(normal, vecMake3(0.0f, 0.0f, -1.0f), normalTolerance)) {
                foundEndCap = true;
            }
        }

        if (centroid.y > 0.05f && std::fabs(centroid.x) < sideCentroidTolerance && centroid.z <= 0.0f
            && centroid.z >= -0.5f) {
            if (normalNear(normal, vecMake3(0.0f, 1.0f, 0.0f), normalTolerance)) {
                foundSidePosY = true;
            }
        }

        if (centroid.x < -0.05f && std::fabs(centroid.y) < sideCentroidTolerance && centroid.z <= 0.0f
            && centroid.z >= -0.5f) {
            if (normalNear(normal, vecMake3(-1.0f, 0.0f, 0.0f), normalTolerance)) {
                foundSideNegX = true;
            }
        }
    }

    expectTrue(foundStartCap, "single F start cap normal points +Z");
    expectTrue(foundEndCap, "single F end cap normal points -Z");
    expectTrue(foundSidePosY, "single F +Y side normal points +Y");
    expectTrue(foundSideNegX, "single F -X side normal points -X");
}

} // namespace

void runProceduralMeshTests()
{
    TurtleParams params{};
    params.defaultStepLength = 0.5f;
    params.defaultRadius = 0.1f;

    {
        const TurtleOutput output = turtleFromDefinition("F\n", params);
        expectEqSize(output.segments.size(), 1, "single F produces one segment");
        if (!output.segments.empty()) {
            expectEqSize(output.segments.front().states.size(), 2, "single F segment has start and end states");
        }
    }

    {
        HostMesh mesh{};
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh("F\n", 0, RootTransform{}, mesh),
            "procedural build succeeds for single F axiom");
        expectTrue(!mesh.triangles.empty(), "single F procedural mesh has triangles");
    }

    {
        expectSingleFCylinderOutwardNormals();
    }

    {
        expectEqSize(loftFrameCountFromDefinition("F\n", params), 2, "single F loft uses two knot rings");
        expectEqSize(loftFrameCountFromDefinition("F F\n", params), 3, "F F loft uses three knot rings");
    }

    {
        HostMesh mesh{};
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh("F(2, 1, 1)\n", 0, RootTransform{}, mesh),
            "procedural build succeeds for F(2, 1, 1)");
        expectTrue(!mesh.triangles.empty(), "F(2, 1, 1) mesh has triangles");
        expectTrue(
            !meshHasInteriorAxisTriangles(
                mesh,
                Vec3{0.0f, 0.0f, 0.0f},
                Vec3{0.0f, 0.0f, -2.0f},
                1.0f),
            "F(2, 1, 1) mesh has no interior axis-near rogue triangles");
    }

    {
        HostMesh mesh{};
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh("F(0, 1)\n", 0, RootTransform{}, mesh),
            "procedural build succeeds for F(0, 1)");
        expectTrue(!mesh.triangles.empty(), "F(0, 1) mesh has triangles");
        expectSphereDiameter(mesh, 2.0f, 0.08f, "F(0, 1) mesh sphere diameter");
    }

    {
        HostMesh mesh{};
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh("F(0, 1, 1)\n", 0, RootTransform{}, mesh),
            "procedural build succeeds for F(0, 1, 1)");
        expectTrue(!mesh.triangles.empty(), "F(0, 1, 1) mesh has triangles");
        expectSphereDiameter(mesh, 2.0f, 0.08f, "F(0, 1, 1) mesh sphere diameter");
    }

    {
        HostMesh mesh{};
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh("F(0)\n", 0, RootTransform{}, mesh),
            "procedural build succeeds for F(0)");
        expectTrue(!mesh.triangles.empty(), "F(0) mesh has triangles");
        expectSphereDiameter(mesh, 2.0f * params.defaultRadius, 0.08f, "F(0) mesh uses defaultRadius sphere");
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
            expectNear(endPosition.z, -1.5f, 1e-5f, "F f F ends after two drawn and one pen-up step");
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
            TurtleState{Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f}, 0.2f},
            TurtleState{Vec3{0.0f, 0.0f, -1.0f}, Vec3{0.0f, 0.0f, -1.0f}, 0.1f}};
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
        HostMesh mesh{};
        const std::string definition =
            "F(0.8) Yaw(2) F(0.5) Yaw(1) F(0.3) Yaw(3) [Pitch(45) F F F][Pitch(-45) F F F]";
        expectTrue(
            ProceduralMeshBuilder::buildHostMesh(definition, 0, RootTransform{}, mesh),
            "procedural build succeeds");
        expectTrue(!mesh.triangles.empty(), "procedural mesh has triangles");

        MeshAccelScene scene;
        expectTrue(scene.build(mesh), "mesh accel scene builds from procedural mesh");
        expectTrue(!scene.trianglesHost().empty(), "procedural mesh triangle count positive");
    }

    {
        std::vector<std::unique_ptr<ScenePrimitive>> primitives;
        std::vector<ProceduralInstance> instances;
        ProceduralInstance instance{};
        instance.commandString = "F F F";
        instance.iterations = 0;
        instances.push_back(std::move(instance));

        MeshAccelScene scene;
        expectTrue(meshSceneBuild(primitives, instances, scene), "meshSceneBuild with procedural instance");
    }
}
