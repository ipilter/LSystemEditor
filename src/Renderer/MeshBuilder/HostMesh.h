#pragma once

#include "Geometry/GeometryTypes.h"
#include "MeshAccel/MeshAccelTypes.h"

#include <cstdint>
#include <vector>

struct HostTriangle
{
    Vec3 v0{};
    Vec3 v1{};
    Vec3 v2{};
    uint32_t materialIndex = 0;
};

struct HostMesh
{
    std::vector<HostTriangle> triangles;
    std::vector<MaterialGpu> materials;
};

struct HostMeshAabb
{
    Vec3 min{};
    Vec3 max{};
};

inline HostMeshAabb hostMeshComputeAabb(const HostMesh& mesh)
{
    HostMeshAabb aabb{};
    if (mesh.triangles.empty()) {
        return aabb;
    }

    const HostTriangle& first = mesh.triangles.front();
    aabb.min = first.v0;
    aabb.max = first.v0;

    auto expand = [&aabb](Vec3 p) {
        if (p.x < aabb.min.x) {
            aabb.min.x = p.x;
        }
        if (p.y < aabb.min.y) {
            aabb.min.y = p.y;
        }
        if (p.z < aabb.min.z) {
            aabb.min.z = p.z;
        }
        if (p.x > aabb.max.x) {
            aabb.max.x = p.x;
        }
        if (p.y > aabb.max.y) {
            aabb.max.y = p.y;
        }
        if (p.z > aabb.max.z) {
            aabb.max.z = p.z;
        }
    };

    for (const HostTriangle& tri : mesh.triangles) {
        expand(tri.v0);
        expand(tri.v1);
        expand(tri.v2);
    }

    return aabb;
}

inline void hostMeshAppend(HostMesh& dst, const HostMesh& src, const uint32_t materialIndexOffset = 0)
{
    const size_t triOffset = dst.triangles.size();
    dst.triangles.insert(dst.triangles.end(), src.triangles.begin(), src.triangles.end());
    for (size_t i = triOffset; i < dst.triangles.size(); ++i) {
        dst.triangles[i].materialIndex += materialIndexOffset;
    }

    dst.materials.insert(dst.materials.end(), src.materials.begin(), src.materials.end());
}
