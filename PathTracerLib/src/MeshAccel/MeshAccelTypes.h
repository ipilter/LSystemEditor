#pragma once

#include "Geometry/GeometryTypes.h"

#include <cstddef>
#include <cstdint>

enum class MeshAccelBoundsOverlayMode : int
{
    Off = 0,
    Bvh = 1,
};

enum MeshBvhNodeFlags : uint32_t
{
    MeshBvhFlagLeaf = 1u << 0,
};

struct alignas(16) MaterialGpu
{
    float r = 0.8f;
    float g = 0.8f;
    float b = 0.8f;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emission = 0.0f;
    float ior = 1.5f;
    float transmission = 0.0f;
};

static_assert(sizeof(MaterialGpu) % alignof(MaterialGpu) == 0);

struct alignas(16) TriangleGpu
{
    Vec3 v0{};
    Vec3 v1{};
    Vec3 v2{};
    Vec3 n0{};
    Vec3 n1{};
    Vec3 n2{};
    uint32_t materialIndex = 0;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
    uint32_t _pad2 = 0;
};

static_assert(sizeof(TriangleGpu) == 96);

struct alignas(16) MeshBvhNode
{
    Vec3 boundsMin{};

    float _pad0 = 0.0f;

    Vec3 boundsMax{};

    float _pad1 = 0.0f;

    uint32_t leftIndex = 0;
    uint32_t rightIndex = 0;
    uint32_t triStart = 0;
    uint32_t triCount = 0;
};

static_assert(sizeof(MeshBvhNode) == 48);
static_assert(offsetof(MeshBvhNode, leftIndex) == 32);

struct alignas(16) MeshAccelSceneGpu
{
    const MeshBvhNode* bvhNodes = nullptr;
    const TriangleGpu* triangles = nullptr;
    const MaterialGpu* materials = nullptr;
    const uint32_t* emissiveTriangleIndices = nullptr;
    uint32_t bvhNodeCount = 0;
    uint32_t triangleCount = 0;
    uint32_t bvhRootIndex = 0;
    uint32_t materialCount = 0;
    uint32_t emissiveTriangleCount = 0;
};

static_assert(sizeof(MeshAccelSceneGpu) == 64);

struct MeshHit
{
    bool hit = false;
    float t = 0.0f;
    Vec3 normal{};
    uint32_t triangleIndex = 0;
};
