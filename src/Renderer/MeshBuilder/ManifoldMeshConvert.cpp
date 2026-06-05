#include "ManifoldMeshConvert.h"

#include <manifold/manifold.h>

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
