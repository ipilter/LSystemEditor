#pragma once

#include "SdfTypes.h"

#include <cstddef>
#include <cstdint>

enum SdfObjectFlags : uint32_t
{
    SdfObjectFlagAnalytical = 1u << 0,
};

enum SdfBvhNodeFlags : uint32_t
{
    SdfBvhFlagLeaf = 1 << 0,
};

struct SdfAccelBuildParams
{
    float boundsPadding = 1.0e-4f;
};

struct alignas(16) SdfAccelObjectGpu
{
    uint32_t payloadIndex = 0;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
    uint32_t flags = 0;

    SdfFloat3 center{};

    float _pad2 = 0.0f;

    SdfFloat3 boundsMin{};

    float _pad3 = 0.0f;

    SdfFloat3 boundsMax{};

    float _pad4 = 0.0f;
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
static_assert(offsetof(SdfBvhNode, leftIndex) == 32);

struct alignas(16) SdfAccelSceneGpu
{
    const SdfBvhNode* bvhNodes = nullptr;
    const SdfAccelObjectGpu* objects = nullptr;
    const SdfAccelPayloadGpu* payloads = nullptr;
    uint32_t bvhNodeCount = 0;
    uint32_t objectCount = 0;
    uint32_t payloadCount = 0;
    uint32_t bvhRootIndex = 0;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
    uint32_t _pad2 = 0;
};

static_assert(sizeof(SdfAccelSceneGpu) == 64);
