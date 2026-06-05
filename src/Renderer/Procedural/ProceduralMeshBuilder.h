#pragma once

#include "HostMesh.h"
#include "ProceduralTypes.h"

#include <cstddef>
#include <string_view>

class ProceduralMeshBuilder
{
public:
    static bool buildHostMesh(
        std::string_view definition,
        std::size_t iterations,
        const RootTransform& root,
        HostMesh& outMesh,
        const ProceduralBuildParams& params = ProceduralBuildParams{});
};
