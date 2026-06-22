#include "MeshSmoothNormals.h"

#include "Geometry/MathCore.h"

#include "SceneUnits.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {

struct CornerRef
{
    size_t triIndex = 0;
    int corner = 0;
};

struct QuantizedKey
{
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;

    bool operator==(const QuantizedKey& other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct QuantizedKeyHash
{
    size_t operator()(const QuantizedKey& key) const
    {
        const size_t hx = static_cast<size_t>(key.x) * 73856093u;
        const size_t hy = static_cast<size_t>(key.y) * 19349663u;
        const size_t hz = static_cast<size_t>(key.z) * 83492791u;
        return hx ^ hy ^ hz;
    }
};

QuantizedKey quantizePosition(Vec3 position, float cellSize)
{
    const float invCell = 1.0f / cellSize;
    return QuantizedKey{
        static_cast<int64_t>(std::floor(position.x * invCell + 0.5f)),
        static_cast<int64_t>(std::floor(position.y * invCell + 0.5f)),
        static_cast<int64_t>(std::floor(position.z * invCell + 0.5f))};
}

Vec3 triangleCornerPosition(const MeshTriangle& tri, int corner)
{
    if (corner == 1) {
        return tri.v1;
    }
    if (corner == 2) {
        return tri.v2;
    }
    return tri.v0;
}

Vec3& triangleCornerNormal(MeshTriangle& tri, int corner)
{
    if (corner == 1) {
        return tri.n1;
    }
    if (corner == 2) {
        return tri.n2;
    }
    return tri.n0;
}

Vec3 faceNormalFromTriangle(const MeshTriangle& tri)
{
    const Vec3 e1 = vecSub3(tri.v1, tri.v0);
    const Vec3 e2 = vecSub3(tri.v2, tri.v0);
    return vecNormalize3(vecCross3(e1, e2));
}

float weldCellSize(const Mesh& mesh)
{
    const MeshAabb aabb = meshComputeAabb(mesh);
    const float extent = vecMax3(
        aabb.max.x - aabb.min.x,
        aabb.max.y - aabb.min.y,
        aabb.max.z - aabb.min.z);
    return SceneUnits::normalWeldCellSizeMm(extent);
}

} // namespace

void meshAssignSmoothNormals(Mesh& mesh, float creaseAngleDeg)
{
    if (mesh.triangles.empty()) {
        return;
    }

    const float clampedCrease = vecClamp(creaseAngleDeg, 0.0f, 180.0f);
    const float cosThreshold = std::cos(clampedCrease * 3.14159265f / 180.0f);
    const float cellSize = weldCellSize(mesh);

    std::vector<Vec3> faceNormals;
    faceNormals.reserve(mesh.triangles.size());
    for (const MeshTriangle& tri : mesh.triangles) {
        faceNormals.push_back(faceNormalFromTriangle(tri));
    }

    std::unordered_map<QuantizedKey, std::vector<CornerRef>, QuantizedKeyHash> buckets;
    buckets.reserve(mesh.triangles.size() * 2);

    for (size_t triIndex = 0; triIndex < mesh.triangles.size(); ++triIndex) {
        const MeshTriangle& tri = mesh.triangles[triIndex];
        for (int corner = 0; corner < 3; ++corner) {
            const QuantizedKey key = quantizePosition(triangleCornerPosition(tri, corner), cellSize);
            buckets[key].push_back(CornerRef{triIndex, corner});
        }
    }

    for (const auto& entry : buckets) {
        const std::vector<CornerRef>& corners = entry.second;
        if (corners.size() <= 1) {
            continue;
        }

        for (const CornerRef& ref : corners) {
            const Vec3 sourceNormal = faceNormals[ref.triIndex];
            Vec3 sum{};
            for (const CornerRef& other : corners) {
                const Vec3 otherNormal = faceNormals[other.triIndex];
                if (vecDot3(sourceNormal, otherNormal) >= cosThreshold) {
                    sum = vecAdd3(sum, otherNormal);
                }
            }

            Vec3 smoothNormal = vecNormalize3(sum);
            if (vecLength3(sum) <= 1.0e-8f) {
                smoothNormal = sourceNormal;
            }

            Vec3& cornerNormal = triangleCornerNormal(mesh.triangles[ref.triIndex], ref.corner);
            const Vec3 existingNormal = cornerNormal;
            if (vecLength3(existingNormal) > 1.0e-4f) {
                const Vec3 existingUnit = vecNormalize3(existingNormal);
                if (vecDot3(existingUnit, smoothNormal) < cosThreshold) {
                    continue;
                }
            }
            cornerNormal = smoothNormal;
        }
    }
}
