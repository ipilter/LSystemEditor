#pragma once

#include "MeshAccel/Mesh.h"
#include "ProceduralTypes.h"

#include <cstddef>
#include <string>
#include <string_view>

class ProceduralMeshBuilder
{
public:
    static bool buildHostMesh(
        std::string_view definition,
        std::size_t iterations,
        const RootTransform& root,
        Mesh& outMesh,
        const ProceduralBuildParams& params = ProceduralBuildParams{},
        std::string* outError = nullptr);

    static void applyRootTransform(Mesh& mesh, const RootTransform& root);
};
