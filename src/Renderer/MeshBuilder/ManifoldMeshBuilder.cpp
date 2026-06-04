#include "ManifoldMeshBuilder.h"

#include <manifold/manifold.h>

namespace {

HostMesh meshFromManifold(const manifold::Manifold& manifoldMesh)
{
    HostMesh mesh{};
    const manifold::MeshGL gl = manifoldMesh.GetMeshGL();
    if (gl.triVerts.empty() || gl.numProp < 3) {
        return mesh;
    }

    const size_t numTris = gl.triVerts.size() / 3;
    const uint64_t numProp = gl.numProp;
    mesh.triangles.reserve(numTris);

    auto vertexAt = [&gl, numProp](uint32_t index) -> Vec3 {
        const size_t base = static_cast<size_t>(index) * static_cast<size_t>(numProp);
        return Vec3{
            gl.vertProperties[base + 0],
            gl.vertProperties[base + 1],
            gl.vertProperties[base + 2]};
    };

    for (size_t tri = 0; tri < numTris; ++tri) {
        const uint32_t i0 = gl.triVerts[tri * 3 + 0];
        const uint32_t i1 = gl.triVerts[tri * 3 + 1];
        const uint32_t i2 = gl.triVerts[tri * 3 + 2];
        mesh.triangles.push_back(HostTriangle{vertexAt(i0), vertexAt(i1), vertexAt(i2)});
    }

    return mesh;
}

manifold::Manifold primitiveToManifold(const ScenePrimitive& primitive, int circularSegments)
{
    const int segments = circularSegments < 3 ? 3 : circularSegments;
    const manifold::vec3 center(primitive.center.x, primitive.center.y, primitive.center.z);

    switch (primitive.type) {
    case PrimitiveType::Sphere:
        return manifold::Manifold::Sphere(primitive.radius, segments).Translate(center);

    case PrimitiveType::Cylinder: {
        const double height = static_cast<double>(2.0f * primitive.halfExtents.y);
        const double radius = static_cast<double>(primitive.halfExtents.x);
        return manifold::Manifold::Cylinder(height, radius, radius, segments, false)
            .Rotate(-90.0, 0.0, 0.0)
            .Translate(center);
    }

    case PrimitiveType::CappedCone: {
        const double height = static_cast<double>(2.0f * primitive.halfHeight);
        return manifold::Manifold::Cylinder(
                   height,
                   static_cast<double>(primitive.radiusBottom),
                   static_cast<double>(primitive.radiusTop),
                   segments,
                   false)
            .Rotate(-90.0, 0.0, 0.0)
            .Translate(center);
    }
    }

    return manifold::Manifold();
}

} // namespace

bool ManifoldMeshBuilder::buildSceneMesh(
    const std::vector<std::unique_ptr<ScenePrimitive>>& primitives,
    HostMesh& outMesh,
    const ManifoldMeshBuildParams& params)
{
    outMesh.triangles.clear();

    const int segments = params.circularSegments < 3 ? 3 : params.circularSegments;

    for (const std::unique_ptr<ScenePrimitive>& primitive : primitives) {
        if (primitive == nullptr) {
            continue;
        }

        manifold::Manifold part = primitiveToManifold(*primitive, segments);
        if (part.Status() != manifold::Manifold::Error::NoError || part.NumTri() == 0) {
            return false;
        }

        part = part.CalculateNormals(0);
        const HostMesh meshPart = meshFromManifold(part);
        if (meshPart.triangles.empty()) {
            return false;
        }

        hostMeshAppend(outMesh, meshPart);
    }

    return !outMesh.triangles.empty();
}
