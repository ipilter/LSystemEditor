#include "MeshSceneContent.h"

bool meshSceneBuildFromPrimitives(
    const std::vector<std::unique_ptr<ScenePrimitive>>& primitives,
    MeshAccelScene& scene,
    const ManifoldMeshBuildParams& params)
{
    HostMesh mesh{};
    if (!ManifoldMeshBuilder::buildSceneMesh(primitives, mesh, params)) {
        return false;
    }

    return scene.build(mesh);
}
