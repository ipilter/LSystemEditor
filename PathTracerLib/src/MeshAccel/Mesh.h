#pragma once

#include "Geometry/GeometryTypes.h"
#include "MeshAccel/MeshAccelTypes.h"

#include <cstdint>
#include <vector>

struct MeshTriangle
{
    Vec3 v0{};
    Vec3 v1{};
    Vec3 v2{};
    Vec3 n0{};
    Vec3 n1{};
    Vec3 n2{};
    Vec2 uv0{};
    Vec2 uv1{};
    Vec2 uv2{};
    uint32_t materialIndex = 0;
};

struct Mesh
{
    std::vector<MeshTriangle> triangles;
    std::vector<MaterialGpu> materials;
    std::vector<TextureDescGpu> textures;
};

struct MeshAabb
{
    Vec3 min{};
    Vec3 max{};
};

inline MeshAabb meshComputeAabb(const Mesh& mesh)
{
    MeshAabb aabb{};
    if (mesh.triangles.empty()) {
        return aabb;
    }

    const MeshTriangle& first = mesh.triangles.front();
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

    for (const MeshTriangle& tri : mesh.triangles) {
        expand(tri.v0);
        expand(tri.v1);
        expand(tri.v2);
    }

    return aabb;
}

inline void meshAppend(Mesh& dst, const Mesh& src, const uint32_t materialIndexOffset = 0)
{
    const size_t triOffset = dst.triangles.size();
    dst.triangles.insert(dst.triangles.end(), src.triangles.begin(), src.triangles.end());
    for (size_t i = triOffset; i < dst.triangles.size(); ++i) {
        dst.triangles[i].materialIndex += materialIndexOffset;
    }

    const uint32_t textureIndexOffset = static_cast<uint32_t>(dst.textures.size());
    dst.textures.insert(dst.textures.end(), src.textures.begin(), src.textures.end());

    const size_t materialOffset = dst.materials.size();
    dst.materials.insert(dst.materials.end(), src.materials.begin(), src.materials.end());
    for (size_t i = materialOffset; i < dst.materials.size(); ++i) {
        MaterialGpu& material = dst.materials[i];
        if (material.albedoTex != 0u) {
            material.albedoTex += textureIndexOffset;
        }
        if (material.roughnessTex != 0u) {
            material.roughnessTex += textureIndexOffset;
        }
        if (material.metallicTex != 0u) {
            material.metallicTex += textureIndexOffset;
        }
        if (material.transmissionTex != 0u) {
            material.transmissionTex += textureIndexOffset;
        }
        if (material.thinTex != 0u) {
            material.thinTex += textureIndexOffset;
        }
        if (material.iorTex != 0u) {
            material.iorTex += textureIndexOffset;
        }
        if (material.subsurfaceTex != 0u) {
            material.subsurfaceTex += textureIndexOffset;
        }
        if (material.emissionTex != 0u) {
            material.emissionTex += textureIndexOffset;
        }
        if (material.diffuseRoughnessTex != 0u) {
            material.diffuseRoughnessTex += textureIndexOffset;
        }
        if (material.scatterRadiusRTex != 0u) {
            material.scatterRadiusRTex += textureIndexOffset;
        }
        if (material.scatterRadiusGTex != 0u) {
            material.scatterRadiusGTex += textureIndexOffset;
        }
        if (material.scatterRadiusBTex != 0u) {
            material.scatterRadiusBTex += textureIndexOffset;
        }
        if (material.specularTex != 0u) {
            material.specularTex += textureIndexOffset;
        }
    }
}
