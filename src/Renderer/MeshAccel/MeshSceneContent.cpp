#include "MeshSceneContent.h"

#include "Procedural/ProceduralMeshBuilder.h"

bool meshSceneBuild(
    const std::vector<ProceduralInstance>& proceduralInstances,
    MeshAccelScene& scene,
    const MeshSceneBuildParams& params)
{
    HostMesh mesh{};
    bool hasMesh = false;

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

        const uint32_t materialIndexOffset = static_cast<uint32_t>(mesh.materials.size());
        hostMeshAppend(mesh, proceduralMesh, materialIndexOffset);
        hasMesh = true;
    }

    if (!hasMesh) {
        return scene.build(HostMesh{});
    }

    return scene.build(mesh);
}
