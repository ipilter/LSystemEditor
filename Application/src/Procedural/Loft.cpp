#include "Loft.h"

#include "Geometry/MathCore.h"

#include <cmath>
#include <cstdint>
#include <vector>

struct LoftCorner
{
    Vec3 position{};
    Vec3 normal{};
    Vec2 uv{};
};

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

LoftCorner sideWallCorner(
    const PathFrame& frame,
    int segment,
    int sideColumns,
    int circularSegments,
    float vCoord)
{
    const int geomSegment = segment < circularSegments ? segment : 0;
    const Vec3 position = ringVertex(frame, geomSegment, circularSegments);
    const Vec3 normal = vecNormalize3(vecSub3(position, frame.position));
    const float uCoord = static_cast<float>(segment) / static_cast<float>(circularSegments);
    return LoftCorner{position, normal, Vec2{uCoord, vCoord}};
}

LoftCorner capRimCorner(const PathFrame& frame, int segment, int circularSegments, float normalSign)
{
    const Vec3 position = ringVertex(frame, segment, circularSegments);
    const Vec2 uv = planarCapUv(segment, circularSegments);
    const Vec3 normal = vecNormalize3(vecScale3(frame.tangent, normalSign));
    return LoftCorner{position, normal, uv};
}

LoftCorner capCenterCorner(const PathFrame& frame, float normalSign)
{
    const Vec3 normal = vecNormalize3(vecScale3(frame.tangent, normalSign));
    return LoftCorner{frame.position, normal, Vec2{0.5f, 0.5f}};
}

void appendHostTriangle(HostMesh& mesh, LoftCorner c0, LoftCorner c1, LoftCorner c2)
{
    HostTriangle tri{};
    tri.v0 = c0.position;
    tri.v1 = c1.position;
    tri.v2 = c2.position;
    tri.n0 = c0.normal;
    tri.n1 = c1.normal;
    tri.n2 = c2.normal;
    tri.uv0 = c0.uv;
    tri.uv1 = c1.uv;
    tri.uv2 = c2.uv;
    mesh.triangles.push_back(tri);
}

HostMesh buildLoftHostMeshFromFrames(const std::vector<PathFrame>& frames, int circularSegments)
{
    HostMesh mesh{};
    if (frames.size() < 2 || circularSegments < 3) {
        return mesh;
    }

    const int ringCount = static_cast<int>(frames.size());
    const int sideColumns = circularSegments + 1;
    const float invRingSpan = ringCount > 1 ? 1.0f / static_cast<float>(ringCount - 1) : 0.0f;

    mesh.triangles.reserve(static_cast<size_t>((ringCount - 1) * circularSegments * 2 + circularSegments * 2));

    for (int ring = 0; ring < ringCount - 1; ++ring) {
        const float v0 = static_cast<float>(ring) * invRingSpan;
        const float v1 = static_cast<float>(ring + 1) * invRingSpan;
        const PathFrame& frame0 = frames[static_cast<size_t>(ring)];
        const PathFrame& frame1 = frames[static_cast<size_t>(ring + 1)];

        for (int segment = 0; segment < circularSegments; ++segment) {
            const int nextSegment = segment + 1;
            const LoftCorner v00 = sideWallCorner(frame0, segment, sideColumns, circularSegments, v0);
            const LoftCorner v01 = sideWallCorner(frame0, nextSegment, sideColumns, circularSegments, v0);
            const LoftCorner v10 = sideWallCorner(frame1, segment, sideColumns, circularSegments, v1);
            const LoftCorner v11 = sideWallCorner(frame1, nextSegment, sideColumns, circularSegments, v1);
            appendHostTriangle(mesh, v00, v01, v10);
            appendHostTriangle(mesh, v01, v11, v10);
        }
    }

    const PathFrame& startFrame = frames.front();
    const PathFrame& endFrame = frames.back();
    const LoftCorner startCenter = capCenterCorner(startFrame, -1.0f);
    const LoftCorner endCenter = capCenterCorner(endFrame, 1.0f);

    for (int segment = 0; segment < circularSegments; ++segment) {
        const int nextSegment = (segment + 1) % circularSegments;
        const LoftCorner startRim0 = capRimCorner(startFrame, segment, circularSegments, -1.0f);
        const LoftCorner startRim1 = capRimCorner(startFrame, nextSegment, circularSegments, -1.0f);
        appendHostTriangle(mesh, startCenter, startRim1, startRim0);

        const LoftCorner endRim0 = capRimCorner(endFrame, segment, circularSegments, 1.0f);
        const LoftCorner endRim1 = capRimCorner(endFrame, nextSegment, circularSegments, 1.0f);
        appendHostTriangle(mesh, endCenter, endRim0, endRim1);
    }

    return mesh;
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

HostMesh buildLoftHostMesh(const SplinePath& path, const ProceduralBuildParams& params)
{
    const std::vector<PathFrame> frames = path.computeLoftFrames(params.samplesPerSpan);
    if (frames.size() < 2) {
        return HostMesh{};
    }

    return buildLoftHostMeshFromFrames(frames, params.circularSegments);
}

HostMesh loftHostMeshFromSegment(const TurtleSegment& segment, const ProceduralBuildParams& params)
{
    SplinePath path;
    if (!path.buildFromSegment(segment, params.hermiteTension)) {
        return HostMesh{};
    }

    if (path.totalArcLength() <= 1e-6f) {
        return HostMesh{};
    }

    return buildLoftHostMesh(path, params);
}
