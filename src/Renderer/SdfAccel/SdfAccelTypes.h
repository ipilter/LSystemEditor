#pragma once

#include "SdfTypes.h"

#include <cstddef>
#include <cstdint>

enum class SdfAccelEvalKind : uint32_t
{
    AnalyticPrimitive = 0,
    SubdivisionSurface = 1,
};

enum class SdfAccelPrimitiveType : uint32_t
{
    Sphere = 0,
    Cylinder = 1,
    CappedCone = 2,
};

enum SdfOctreeNodeFlags : uint8_t
{
    SdfOctreeFlagLeaf = 1 << 0,
    SdfOctreeFlagStraddlesSurface = 1 << 1,
    SdfOctreeFlagInsideSolid = 1 << 2,
};

enum SdfBvhNodeFlags : uint32_t
{
    SdfBvhFlagLeaf = 1 << 0,
};

constexpr uint32_t sdfAccelPackChildMaskAndFlags(uint8_t childMask, uint8_t flags)
{
    return static_cast<uint32_t>(childMask) | (static_cast<uint32_t>(flags) << 8);
}

constexpr uint8_t sdfAccelChildMaskFromPacked(uint32_t packed)
{
    return static_cast<uint8_t>(packed & 0xFFu);
}

constexpr uint8_t sdfAccelFlagsFromPacked(uint32_t packed)
{
    return static_cast<uint8_t>((packed >> 8) & 0xFFu);
}

struct SdfAccelBuildParams
{
    int maxDepth = 12;
    float maxSurfaceError = 1.0e-3f;
    float boundsPadding = 1.0e-4f;
    float pruneEpsilon = 1.0e-4f;
};

struct alignas(16) SdfOctreeNode
{
    uint32_t firstChildIndex = 0;
    uint32_t childMaskAndFlags = 0;
    float dMin = 0.0f;
    float dMax = 0.0f;
    SdfFloat3 center{};
    float _pad0 = 0.0f;
    SdfFloat3 halfExtent{};
    float _pad1 = 0.0f;
};

static_assert(sizeof(SdfOctreeNode) == 48);
static_assert(offsetof(SdfOctreeNode, dMin) == 8);
static_assert(offsetof(SdfOctreeNode, center) == 16);
static_assert(offsetof(SdfOctreeNode, halfExtent) == 32);

struct alignas(16) SdfAccelObjectGpu
{
    uint32_t evalKind = 0;
    uint32_t payloadIndex = 0;
    uint32_t octreeNodeOffset = 0;
    uint32_t octreeRootIndex = 0;
    SdfFloat3 center{};
    float _pad0 = 0.0f;
    SdfFloat3 boundsMin{};
    float _pad1 = 0.0f;
    SdfFloat3 boundsMax{};
    float _pad2 = 0.0f;
};

static_assert(sizeof(SdfAccelObjectGpu) == 64);

struct alignas(16) SdfAccelPayloadGpu
{
    uint32_t type = 0;
    uint32_t _pad0 = 0;
    float param0 = 0.0f;
    float param1 = 0.0f;
    float param2 = 0.0f;
    float param3 = 0.0f;
    SdfFloat2 halfExtents{};
    SdfFloat2 _pad1{};
};

static_assert(sizeof(SdfAccelPayloadGpu) == 48);

struct alignas(16) SdfBvhNode
{
    SdfFloat3 boundsMin{};
    float _pad0 = 0.0f;
    SdfFloat3 boundsMax{};
    float _pad1 = 0.0f;
    uint32_t leftIndex = 0;
    uint32_t rightIndex = 0;
    uint32_t objectIndex = 0;
    uint32_t flags = 0;
};

static_assert(sizeof(SdfBvhNode) == 48);

struct alignas(16) SdfAccelSceneGpu
{
    const SdfBvhNode* bvhNodes = nullptr;
    const SdfOctreeNode* octreeNodes = nullptr;
    const SdfAccelObjectGpu* objects = nullptr;
    const SdfAccelPayloadGpu* payloads = nullptr;
    uint32_t bvhNodeCount = 0;
    uint32_t octreeNodeCount = 0;
    uint32_t objectCount = 0;
    uint32_t payloadCount = 0;
    uint32_t bvhRootIndex = 0;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
    uint32_t _pad2 = 0;
};

static_assert(sizeof(SdfAccelSceneGpu) == 64);
