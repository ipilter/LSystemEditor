#pragma once

#include "MeshAccelTypes.h"

#include <cstdint>
#include <vector>

bool meshAccelBuildBvh(
    const std::vector<TriangleGpu>& triangles,
    std::vector<MeshBvhNode>& outNodes,
    uint32_t& outRootIndex);
