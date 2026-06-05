#include "MeshSceneContent.h"

#include "Procedural/ProceduralMeshBuilder.h"

bool meshSceneBuildFromPrimitives(
    const std::vector<std::unique_ptr<ScenePrimitive>>& primitives,
    MeshAccelScene& scene,
    const ManifoldMeshBuildParams& params)
{
    return meshSceneBuild(primitives, {}, scene, params);
}

bool meshSceneBuild(
    const std::vector<std::unique_ptr<ScenePrimitive>>& primitives,
    const std::vector<ProceduralInstance>& proceduralInstances,
    MeshAccelScene& scene,
    const ManifoldMeshBuildParams& params)
{
    HostMesh mesh{};
    bool hasMesh = false;

    if (!primitives.empty()) {
        HostMesh primitiveMesh{};
        if (!ManifoldMeshBuilder::buildSceneMesh(primitives, primitiveMesh, params)) {
            return false;
        }
        hostMeshAppend(mesh, primitiveMesh);
        hasMesh = true;
    }

    ProceduralBuildParams proceduralParams{};
    proceduralParams.circularSegments = params.circularSegments;

    for (const ProceduralInstance& instance : proceduralInstances) {
        HostMesh proceduralMesh{};
        RootTransform root{};
        root.translation = instance.translation;
        root.rotationDeg = instance.rotationDeg;
        if (!ProceduralMeshBuilder::buildHostMesh(
                instance.commandString, instance.iterations, root, proceduralMesh, proceduralParams)) {
            return false;
        }
        hostMeshAppend(mesh, proceduralMesh);
        hasMesh = true;
    }

    if (!hasMesh) {
        return scene.build(HostMesh{});
    }

    return scene.build(mesh);
}
