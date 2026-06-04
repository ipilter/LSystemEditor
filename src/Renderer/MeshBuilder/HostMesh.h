#pragma once

#include "Geometry/GeometryTypes.h"

#include <vector>

struct HostTriangle
{
    Vec3 v0{};
    Vec3 v1{};
    Vec3 v2{};
};

struct HostMesh
{
    std::vector<HostTriangle> triangles;
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

inline void hostMeshAppend(HostMesh& dst, const HostMesh& src)
{
    dst.triangles.insert(dst.triangles.end(), src.triangles.begin(), src.triangles.end());
}
