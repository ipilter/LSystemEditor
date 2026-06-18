#include "MeshSceneContent.h"

#include "Procedural/ProceduralMeshBuilder.h"

bool meshSceneBuild(
    const std::vector<ProceduralInstance>& proceduralInstances,
    MeshAccelScene& scene,
    const MeshSceneBuildParams& params,
    QString* outError)
{
    HostMesh mesh{};
    bool hasMesh = false;

    ProceduralBuildParams proceduralParams{};
    proceduralParams.circularSegments = params.circularSegments;
    proceduralParams.creaseAngleDeg = params.creaseAngleDeg;

    for (std::size_t instanceIndex = 0; instanceIndex < proceduralInstances.size(); ++instanceIndex) {
        const ProceduralInstance& instance = proceduralInstances[instanceIndex];
        HostMesh proceduralMesh{};
        RootTransform root{};
        root.translation = instance.translation;
        root.rotationDeg = instance.rotationDeg;
        std::string buildError;
        if (!ProceduralMeshBuilder::buildHostMesh(
                instance.commandString,
                instance.iterations,
                root,
                proceduralMesh,
                proceduralParams,
                &buildError)) {
            if (outError != nullptr) {
                if (!buildError.empty()) {
                    *outError = QStringLiteral("Procedural instance %1: %2")
                                    .arg(instanceIndex)
                                    .arg(QString::fromStdString(buildError));
                }
            }
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
