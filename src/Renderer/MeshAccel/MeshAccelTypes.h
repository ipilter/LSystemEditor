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

struct alignas(16) TriangleGpu
{
    Vec3 v0{};
    Vec3 v1{};
    Vec3 v2{};
    Vec3 normal{};
};

static_assert(sizeof(TriangleGpu) == 48);

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
    uint32_t bvhNodeCount = 0;
    uint32_t triangleCount = 0;
    uint32_t bvhRootIndex = 0;
    uint32_t _pad0 = 0;
};

static_assert(sizeof(MeshAccelSceneGpu) == 32);

struct RenderParamsGpu
{
    float maxDistance = 100.0f;
    float backgroundR = 10.0f / 255.0f;
    float backgroundG = 10.0f / 255.0f;
    float backgroundB = 10.0f / 255.0f;
};

struct MeshHit
{
    bool hit = false;
    float t = 0.0f;
    Vec3 normal{};
};
