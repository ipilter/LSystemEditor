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
    float _pad0 = 0.0f;
    float _pad1 = 0.0f;
};

static_assert(sizeof(MaterialGpu) == 32);

struct alignas(16) TriangleGpu
{
    Vec3 v0{};
    Vec3 v1{};
    Vec3 v2{};
    Vec3 normal{};
    uint32_t materialIndex = 0;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
    uint32_t _pad2 = 0;
};

static_assert(sizeof(TriangleGpu) == 64);

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
    uint32_t bvhNodeCount = 0;
    uint32_t triangleCount = 0;
    uint32_t bvhRootIndex = 0;
    uint32_t materialCount = 0;
};

static_assert(sizeof(MeshAccelSceneGpu) == 48);

struct RenderParamsGpu
{
    float backgroundR = 10.0f / 255.0f;
    float backgroundG = 10.0f / 255.0f;
    float backgroundB = 10.0f / 255.0f;
    float sunAzimuthDeg = 135.0f;
    float sunElevationDeg = 45.0f;
    float sunColorR = 1.0f;
    float sunColorG = 0.96f;
    float sunColorB = 0.9f;
    float sunDiskSizeDeg = 0.53f;
    int secondaryBounceCount = 1;
    int _pad0 = 0;
};

struct MeshHit
{
    bool hit = false;
    float t = 0.0f;
    Vec3 normal{};
    uint32_t triangleIndex = 0;
};
