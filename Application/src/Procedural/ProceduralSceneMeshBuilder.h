#pragma once

#include "Procedural/ProceduralTypes.h"

#include "MeshAccel/Mesh.h"

#include <string>
#include <vector>

bool buildMeshFromInstances(
    const std::vector<ProceduralInstance>& instances,
    const ProceduralBuildParams& params,
    Mesh& outMesh,
    std::string* outError = nullptr);
