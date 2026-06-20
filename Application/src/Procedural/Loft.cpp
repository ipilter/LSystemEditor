#include "Loft.h"

#include "Geometry/MathCore.h"
#include "ManifoldMeshConvert.h"
#include "MeshSmoothNormals.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

bool isValidManifold(const manifold::Manifold& mesh)
{
    return mesh.Status() == manifold::Manifold::Error::NoError && mesh.NumTri() > 0;
}

Vec3 ringVertex(const PathFrame& frame, int segmentIndex, int circularSegments)
{
    const float angle = static_cast<float>(segmentIndex) * 6.283185307f / static_cast<float>(circularSegments);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return vecAdd3(
        frame.position,
        vecAdd3(vecScale3(frame.normal, c * frame.radius), vecScale3(frame.binormal, s * frame.radius)));
}

Vec2 planarCapUv(int segmentIndex, int circularSegments)
{
    const float angle = static_cast<float>(segmentIndex) * 6.283185307f / static_cast<float>(circularSegments);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return Vec2{0.5f + 0.5f * s, 0.5f + 0.5f * c};
}

uint32_t ringIndex(int ring, int segment, int circularSegments)
{
    return static_cast<uint32_t>(ring * circularSegments + segment);
}

void appendTriangle(std::vector<uint32_t>& triVerts, uint32_t i0, uint32_t i1, uint32_t i2)
{
    triVerts.push_back(i0);
    triVerts.push_back(i1);
    triVerts.push_back(i2);
}

void appendVertex(std::vector<float>& vertProperties, Vec3 position, float u, float v)
{
    vertProperties.push_back(position.x);
    vertProperties.push_back(position.y);
    vertProperties.push_back(position.z);
    vertProperties.push_back(0.0f);
    vertProperties.push_back(0.0f);
    vertProperties.push_back(0.0f);
    vertProperties.push_back(u);
    vertProperties.push_back(v);
}

manifold::Manifold buildRingLoftMesh(const std::vector<PathFrame>& frames, int circularSegments)
{
    if (frames.size() < 2 || circularSegments < 3) {
        return manifold::Manifold();
    }

    const int ringCount = static_cast<int>(frames.size());
    const int sideVertexCount = ringCount * circularSegments;
    const uint32_t startCapRimBase = static_cast<uint32_t>(sideVertexCount);
    const uint32_t endCapRimBase = startCapRimBase + static_cast<uint32_t>(circularSegments);
    const uint32_t startCapCenter = endCapRimBase + static_cast<uint32_t>(circularSegments);
    const uint32_t endCapCenter = startCapCenter + 1u;
    const int lastRing = ringCount - 1;
    const float invRingSpan = ringCount > 1 ? 1.0f / static_cast<float>(ringCount - 1) : 0.0f;

    const size_t totalVertices =
        static_cast<size_t>(sideVertexCount + 2 * circularSegments + 2);
    std::vector<float> vertProperties;
    vertProperties.reserve(totalVertices * 8);
    std::vector<uint32_t> triVerts;
    triVerts.reserve(static_cast<size_t>((ringCount - 1) * circularSegments * 6 + circularSegments * 6));

    for (int ring = 0; ring < ringCount; ++ring) {
        const float vCoord = static_cast<float>(ring) * invRingSpan;
        const PathFrame& frame = frames[static_cast<size_t>(ring)];
        for (int segment = 0; segment < circularSegments; ++segment) {
            const float uCoord = static_cast<float>(segment) / static_cast<float>(circularSegments);
            appendVertex(
                vertProperties,
                ringVertex(frame, segment, circularSegments),
                uCoord,
                vCoord);
        }
    }

    const PathFrame& startFrame = frames.front();
    for (int segment = 0; segment < circularSegments; ++segment) {
        const Vec2 capUv = planarCapUv(segment, circularSegments);
        appendVertex(
            vertProperties,
            ringVertex(startFrame, segment, circularSegments),
            capUv.x,
            capUv.y);
    }

    const PathFrame& endFrame = frames.back();
    for (int segment = 0; segment < circularSegments; ++segment) {
        const Vec2 capUv = planarCapUv(segment, circularSegments);
        appendVertex(
            vertProperties,
            ringVertex(endFrame, segment, circularSegments),
            capUv.x,
            capUv.y);
    }

    appendVertex(vertProperties, startFrame.position, 0.5f, 0.5f);
    appendVertex(vertProperties, endFrame.position, 0.5f, 0.5f);

    std::vector<uint32_t> mergeFromVert;
    std::vector<uint32_t> mergeToVert;
    mergeFromVert.reserve(static_cast<size_t>(circularSegments * 2));
    mergeToVert.reserve(static_cast<size_t>(circularSegments * 2));
    for (int segment = 0; segment < circularSegments; ++segment) {
        mergeFromVert.push_back(startCapRimBase + static_cast<uint32_t>(segment));
        mergeToVert.push_back(ringIndex(0, segment, circularSegments));
        mergeFromVert.push_back(endCapRimBase + static_cast<uint32_t>(segment));
        mergeToVert.push_back(ringIndex(lastRing, segment, circularSegments));
    }

    for (int ring = 0; ring < ringCount - 1; ++ring) {
        for (int segment = 0; segment < circularSegments; ++segment) {
            const int nextSegment = (segment + 1) % circularSegments;
            const uint32_t v0 = ringIndex(ring, segment, circularSegments);
            const uint32_t v1 = ringIndex(ring, nextSegment, circularSegments);
            const uint32_t v2 = ringIndex(ring + 1, nextSegment, circularSegments);
            const uint32_t v3 = ringIndex(ring + 1, segment, circularSegments);
            appendTriangle(triVerts, v0, v1, v3);
            appendTriangle(triVerts, v1, v2, v3);
        }
    }

    for (int segment = 0; segment < circularSegments; ++segment) {
        const int nextSegment = (segment + 1) % circularSegments;
        appendTriangle(
            triVerts,
            startCapCenter,
            startCapRimBase + static_cast<uint32_t>(nextSegment),
            startCapRimBase + static_cast<uint32_t>(segment));
    }

    for (int segment = 0; segment < circularSegments; ++segment) {
        const int nextSegment = (segment + 1) % circularSegments;
        appendTriangle(
            triVerts,
            endCapCenter,
            endCapRimBase + static_cast<uint32_t>(segment),
            endCapRimBase + static_cast<uint32_t>(nextSegment));
    }

    manifold::MeshGL mesh{};
    mesh.numProp = 8;
    mesh.vertProperties = std::move(vertProperties);
    mesh.triVerts = std::move(triVerts);
    mesh.mergeFromVert = std::move(mergeFromVert);
    mesh.mergeToVert = std::move(mergeToVert);

    manifold::Manifold result(mesh);
    return result;
}

manifold::Manifold buildSphereMesh(Vec3 center, float radius, int circularSegments)
{
    if (radius <= 1e-6f) {
        return manifold::Manifold();
    }

    const int segments = circularSegments < 3 ? 3 : circularSegments;
    return manifold::Manifold::Sphere(static_cast<double>(radius), segments)
        .Translate(manifold::vec3(center.x, center.y, center.z));
}

} // namespace

float characteristicRadiusFromSegment(const TurtleSegment& segment, const ProceduralBuildParams& params)
{
    float maxRadius = 0.0f;
    for (const TurtleState& state : segment.states) {
        maxRadius = std::max(maxRadius, state.radius);
    }
    if (maxRadius <= 1e-6f) {
        maxRadius = params.turtle.defaultRadius;
    }
    return maxRadius;
}

manifold::Manifold loftOrSphereFromSegment(const TurtleSegment& segment, const ProceduralBuildParams& params)
{
    SplinePath path;
    if (!path.buildFromSegment(segment, params.hermiteTension)) {
        return manifold::Manifold();
    }

    if (path.totalArcLength() <= 1e-6f) {
        if (segment.states.empty()) {
            return manifold::Manifold();
        }

        const Vec3 center = segment.states.front().position;
        float radius = segment.states.front().radius;
        if (radius <= 1e-6f) {
            radius = params.turtle.defaultRadius;
        }

        return buildSphereMesh(center, radius, params.circularSegments);
    }

    return loftSegment(path, params);
}

manifold::Manifold loftSegment(const SplinePath& path, const ProceduralBuildParams& params)
{
    const std::vector<PathFrame> frames = path.computeLoftFrames(params.samplesPerSpan);
    if (frames.size() < 2) {
        return manifold::Manifold();
    }

    return buildRingLoftMesh(frames, params.circularSegments);
}

manifold::Manifold refineManifoldForRender(
    const manifold::Manifold& mesh,
    const ProceduralBuildParams& params,
    float characteristicRadius)
{
    (void)characteristicRadius;
    if (!isValidManifold(mesh) || params.segmentRefineTolerance <= 1e-6f) {
        return mesh;
    }

    // Default segmentRefineTolerance (0.01) skips refinement; circularSegments already
    // controls silhouette quality. Only refine when tolerance is explicitly very low.
    int refinePasses = 0;
    if (params.segmentRefineTolerance < 0.001f) {
        refinePasses = 2;
    } else if (params.segmentRefineTolerance < 0.005f) {
        refinePasses = 1;
    } else {
        return mesh;
    }

    manifold::Manifold refined = mesh;
    for (int pass = 0; pass < refinePasses; ++pass) {
        const uint64_t triangleCountBefore = refined.NumTri();
        manifold::Manifold next = refined.Refine(2);
        if (!isValidManifold(next) || next.NumTri() <= triangleCountBefore) {
            break;
        }
        refined = std::move(next);
    }

    return refined;
}

Mesh renderMeshFromManifold(
    const manifold::Manifold& mesh,
    const ProceduralBuildParams& params,
    float characteristicRadius)
{
    manifold::Manifold renderMesh = refineManifoldForRender(mesh, params, characteristicRadius);
    renderMesh = renderMesh.CalculateNormals(0, static_cast<double>(params.creaseAngleDeg));
    if (!isValidManifold(renderMesh)) {
        return Mesh{};
    }
    Mesh hostMesh = meshFromManifold(renderMesh);
    meshAssignSmoothNormals(hostMesh, params.creaseAngleDeg);
    return hostMesh;
}

Mesh renderMeshFromSegment(const TurtleSegment& segment, const ProceduralBuildParams& params)
{
    const manifold::Manifold segmentMesh = loftOrSphereFromSegment(segment, params);
    if (!isValidManifold(segmentMesh)) {
        return Mesh{};
    }

    return renderMeshFromManifold(
        segmentMesh,
        params,
        characteristicRadiusFromSegment(segment, params));
}
