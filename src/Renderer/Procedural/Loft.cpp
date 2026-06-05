#include "Loft.h"

#include "Geometry/MathCore.h"

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

void appendVertex(std::vector<float>& vertProperties, Vec3 position)
{
    vertProperties.push_back(position.x);
    vertProperties.push_back(position.y);
    vertProperties.push_back(position.z);
}

manifold::Manifold buildRingLoftMesh(const std::vector<PathFrame>& frames, int circularSegments)
{
    if (frames.size() < 2 || circularSegments < 3) {
        return manifold::Manifold();
    }

    const int ringCount = static_cast<int>(frames.size());
    const int sideVertexCount = ringCount * circularSegments;
    const uint32_t startCapCenter = static_cast<uint32_t>(sideVertexCount);
    const uint32_t endCapCenter = static_cast<uint32_t>(sideVertexCount + 1);

    std::vector<float> vertProperties;
    vertProperties.reserve(static_cast<size_t>(sideVertexCount + 2) * 3);
    std::vector<uint32_t> triVerts;
    triVerts.reserve(static_cast<size_t>((ringCount - 1) * circularSegments * 6 + circularSegments * 6));

    for (const PathFrame& frame : frames) {
        for (int segment = 0; segment < circularSegments; ++segment) {
            appendVertex(vertProperties, ringVertex(frame, segment, circularSegments));
        }
    }

    appendVertex(vertProperties, frames.front().position);
    appendVertex(vertProperties, frames.back().position);

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
            ringIndex(0, nextSegment, circularSegments),
            ringIndex(0, segment, circularSegments));
    }

    const int lastRing = ringCount - 1;
    for (int segment = 0; segment < circularSegments; ++segment) {
        const int nextSegment = (segment + 1) % circularSegments;
        appendTriangle(
            triVerts,
            endCapCenter,
            ringIndex(lastRing, segment, circularSegments),
            ringIndex(lastRing, nextSegment, circularSegments));
    }

    manifold::MeshGL mesh{};
    mesh.numProp = 3;
    mesh.vertProperties = std::move(vertProperties);
    mesh.triVerts = std::move(triVerts);

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

    manifold::Manifold result = buildRingLoftMesh(frames, params.circularSegments);
    if (!isValidManifold(result)) {
        return manifold::Manifold();
    }

    manifold::Manifold refined = result.RefineToTolerance(static_cast<double>(params.segmentRefineTolerance));
    if (isValidManifold(refined)) {
        result = std::move(refined);
    }

    return result;
}
