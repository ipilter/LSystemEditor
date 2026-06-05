#pragma once

#include "MeshBuilder/ManifoldMeshBuilder.h"
#include "MeshAccel/MeshAccelScene.h"
#include "Procedural/ProceduralTypes.h"
#include "ScenePrimitive.h"

#include <memory>
#include <vector>

bool meshSceneBuildFromPrimitives(
    const std::vector<std::unique_ptr<ScenePrimitive>>& primitives,
    MeshAccelScene& scene,
    const ManifoldMeshBuildParams& params = ManifoldMeshBuildParams{});

bool meshSceneBuild(
    const std::vector<std::unique_ptr<ScenePrimitive>>& primitives,
    const std::vector<ProceduralInstance>& proceduralInstances,
    MeshAccelScene& scene,
    const ManifoldMeshBuildParams& params = ManifoldMeshBuildParams{});
