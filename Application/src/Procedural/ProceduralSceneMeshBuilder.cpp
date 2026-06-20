#include "ProceduralSceneMeshBuilder.h"

#include "ProceduralMeshBuilder.h"

bool buildMeshFromInstances(
    const std::vector<ProceduralInstance>& instances,
    const ProceduralBuildParams& params,
    Mesh& outMesh,
    std::string* outError)
{
    outMesh = Mesh{};
    bool hasMesh = false;

    for (std::size_t instanceIndex = 0; instanceIndex < instances.size(); ++instanceIndex) {
        const ProceduralInstance& instance = instances[instanceIndex];
        Mesh proceduralMesh{};
        RootTransform root{};
        root.translation = instance.translation;
        root.rotationDeg = instance.rotationDeg;
        std::string buildError;
        if (!ProceduralMeshBuilder::buildHostMesh(
                instance.commandString,
                instance.iterations,
                root,
                proceduralMesh,
                params,
                &buildError)) {
            if (outError != nullptr) {
                if (!buildError.empty()) {
                    *outError = "Procedural instance " + std::to_string(instanceIndex) + ": " + buildError;
                }
            }
            return false;
        }

        const uint32_t materialIndexOffset = static_cast<uint32_t>(outMesh.materials.size());
        meshAppend(outMesh, proceduralMesh, materialIndexOffset);
        hasMesh = true;
    }

    if (!hasMesh) {
        outMesh = Mesh{};
    }

    return true;
}
