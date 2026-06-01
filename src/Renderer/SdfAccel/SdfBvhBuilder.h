#pragma once

#include "SdfAccelTypes.h"

#include <vector>

bool sdfAccelBuildBvh(
    const std::vector<SdfAccelObjectGpu>& objects,
    std::vector<SdfBvhNode>& outNodes,
    uint32_t& outRootIndex);
