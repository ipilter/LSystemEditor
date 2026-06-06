#pragma once

#include "MeshAccel/MeshAccelScene.h"
#include "Procedural/ProceduralTypes.h"

#include <vector>

struct MeshSceneBuildParams
{
    int circularSegments = 32;
    float creaseAngleDeg = 50.0f;
};

bool meshSceneBuild(
    const std::vector<ProceduralInstance>& proceduralInstances,
    MeshAccelScene& scene,
    const MeshSceneBuildParams& params = {});
