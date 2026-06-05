#include "ManifoldMeshBuilder.h"

#include "ManifoldMeshConvert.h"

#include <manifold/manifold.h>

namespace {

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
