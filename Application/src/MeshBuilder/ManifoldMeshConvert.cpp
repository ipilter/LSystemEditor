#include "ManifoldMeshConvert.h"

#include "Geometry/MathCore.h"

#include <cmath>
#include <manifold/manifold.h>

namespace {

Vec3 faceNormalFromTriangle(Vec3 v0, Vec3 v1, Vec3 v2)
{
    const Vec3 e1 = vecSub3(v1, v0);
    const Vec3 e2 = vecSub3(v2, v0);
    return vecNormalize3(vecMake3(
        e1.y * e2.z - e1.z * e2.y,
        e1.z * e2.x - e1.x * e2.z,
        e1.x * e2.y - e1.y * e2.x));
}

Vec2 sphericalUvFromPosition(Vec3 position)
{
    const float len = sqrtf(position.x * position.x + position.y * position.y + position.z * position.z);
    if (len <= 1.0e-8f) {
        return Vec2{0.0f, 0.0f};
    }

    const float u = atan2f(position.z, position.x) * (0.5f / 3.14159265f) + 0.5f;
    const float v = asinf(position.y / len) * (1.0f / 3.14159265f) + 0.5f;
    return Vec2{u, v};
}

} // namespace

Mesh meshFromManifold(const manifold::Manifold& manifoldMesh)
{
    Mesh mesh{};
    const manifold::MeshGL gl = manifoldMesh.GetMeshGL();
    if (gl.triVerts.empty() || gl.numProp < 3) {
        return mesh;
    }

    const size_t numTris = gl.triVerts.size() / 3;
    const uint64_t numProp = gl.numProp;
    const bool hasVertexNormals = numProp >= 6;
    const bool hasUv = numProp >= 8 || numProp == 5;
    mesh.triangles.reserve(numTris);

    auto vertexAt = [&gl, numProp](uint32_t index) -> Vec3 {
        const size_t base = static_cast<size_t>(index) * static_cast<size_t>(numProp);
        return Vec3{
            gl.vertProperties[base + 0],
            gl.vertProperties[base + 1],
            gl.vertProperties[base + 2]};
    };

    auto normalAt = [&gl, numProp](uint32_t index) -> Vec3 {
        const size_t base = static_cast<size_t>(index) * static_cast<size_t>(numProp);
        return vecNormalize3(Vec3{
            gl.vertProperties[base + 3],
            gl.vertProperties[base + 4],
            gl.vertProperties[base + 5]});
    };

    auto uvAt = [&gl, numProp, &vertexAt, hasUv](uint32_t index) -> Vec2 {
        if (!hasUv) {
            return sphericalUvFromPosition(vertexAt(index));
        }

        const size_t base = static_cast<size_t>(index) * static_cast<size_t>(numProp);
        if (numProp >= 8) {
            return Vec2{gl.vertProperties[base + 6], gl.vertProperties[base + 7]};
        }
        return Vec2{gl.vertProperties[base + 3], gl.vertProperties[base + 4]};
    };

    for (size_t tri = 0; tri < numTris; ++tri) {
        const uint32_t i0 = gl.triVerts[tri * 3 + 0];
        const uint32_t i1 = gl.triVerts[tri * 3 + 1];
        const uint32_t i2 = gl.triVerts[tri * 3 + 2];

        const Vec3 v0 = vertexAt(i0);
        const Vec3 v1 = vertexAt(i1);
        const Vec3 v2 = vertexAt(i2);

        MeshTriangle meshTri{};
        meshTri.v0 = v0;
        meshTri.v1 = v1;
        meshTri.v2 = v2;
        meshTri.uv0 = uvAt(i0);
        meshTri.uv1 = uvAt(i1);
        meshTri.uv2 = uvAt(i2);

        if (hasVertexNormals) {
            meshTri.n0 = normalAt(i0);
            meshTri.n1 = normalAt(i1);
            meshTri.n2 = normalAt(i2);
        } else {
            const Vec3 faceNormal = faceNormalFromTriangle(v0, v1, v2);
            meshTri.n0 = faceNormal;
            meshTri.n1 = faceNormal;
            meshTri.n2 = faceNormal;
        }

        mesh.triangles.push_back(meshTri);
    }

    return mesh;
}
