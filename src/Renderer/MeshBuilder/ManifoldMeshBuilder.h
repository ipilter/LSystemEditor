#pragma once

#include "HostMesh.h"
#include "ScenePrimitive.h"

#include <memory>
#include <vector>

struct ManifoldMeshBuildParams
{
    int circularSegments = 32;
};

/// Builds scene geometry from Manifold analytic primitives (per-object mesh, concatenated).
class ManifoldMeshBuilder
{
public:
    static bool buildSceneMesh(
        const std::vector<std::unique_ptr<ScenePrimitive>>& primitives,
        HostMesh& outMesh,
        const ManifoldMeshBuildParams& params = ManifoldMeshBuildParams{});
};
