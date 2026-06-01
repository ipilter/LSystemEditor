#pragma once

#include "SdfAccelField.h"
#include "SdfAccelTypes.h"

#include <vector>

struct SdfOctreeBuildNode
{
    SdfFloat3 center{};
    SdfFloat3 halfExtent{};
    float dMin = 0.0f;
    float dMax = 0.0f;
    uint8_t childMask = 0;
    uint8_t flags = 0;
    int childBuildIndices[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
};

bool sdfAccelBuildOctreeForField(
    const SdfAccelField& field,
    const SdfAccelBuildParams& params,
    std::vector<SdfOctreeNode>& outNodes,
    uint32_t& outRootIndex);
