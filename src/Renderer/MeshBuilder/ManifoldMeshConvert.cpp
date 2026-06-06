#include "ManifoldMeshConvert.h"

#include "Geometry/MathCore.h"

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

} // namespace

HostMesh meshFromManifold(const manifold::Manifold& manifoldMesh)
{
    HostMesh mesh{};
    const manifold::MeshGL gl = manifoldMesh.GetMeshGL();
    if (gl.triVerts.empty() || gl.numProp < 3) {
        return mesh;
    }

    const size_t numTris = gl.triVerts.size() / 3;
    const uint64_t numProp = gl.numProp;
    const bool hasVertexNormals = numProp >= 6;
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

    for (size_t tri = 0; tri < numTris; ++tri) {
        const uint32_t i0 = gl.triVerts[tri * 3 + 0];
        const uint32_t i1 = gl.triVerts[tri * 3 + 1];
        const uint32_t i2 = gl.triVerts[tri * 3 + 2];

        const Vec3 v0 = vertexAt(i0);
        const Vec3 v1 = vertexAt(i1);
        const Vec3 v2 = vertexAt(i2);

        HostTriangle hostTri{};
        hostTri.v0 = v0;
        hostTri.v1 = v1;
        hostTri.v2 = v2;

        if (hasVertexNormals) {
            hostTri.n0 = normalAt(i0);
            hostTri.n1 = normalAt(i1);
            hostTri.n2 = normalAt(i2);
        } else {
            const Vec3 faceNormal = faceNormalFromTriangle(v0, v1, v2);
            hostTri.n0 = faceNormal;
            hostTri.n1 = faceNormal;
            hostTri.n2 = faceNormal;
        }

        mesh.triangles.push_back(hostTri);
    }

    return mesh;
}
