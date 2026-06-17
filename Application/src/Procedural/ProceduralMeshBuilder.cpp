#include "ProceduralMeshBuilder.h"

#include "LSystemEvaluator.h"
#include "LSystemMaterials.h"
#include "Loft.h"
#include "ManifoldMeshConvert.h"
#include "Turtle.h"

#include <manifold/manifold.h>
#include <string>
#include <unordered_map>

namespace {

bool isValidManifold(const manifold::Manifold& mesh)
{
    return mesh.Status() == manifold::Manifold::Error::NoError && mesh.NumTri() > 0;
}

manifold::Manifold applyRootTransform(const manifold::Manifold& mesh, const RootTransform& root)
{
    const double yaw = static_cast<double>(root.rotationDeg.x);
    const double pitch = static_cast<double>(root.rotationDeg.y);
    const double roll = static_cast<double>(root.rotationDeg.z);
    return mesh.Rotate(0.0, yaw, 0.0)
        .Rotate(pitch, 0.0, 0.0)
        .Rotate(0.0, 0.0, roll)
        .Translate(manifold::vec3(root.translation.x, root.translation.y, root.translation.z));
}

MaterialGpu toMaterialGpu(const MaterialEntry& entry)
{
    MaterialGpu material{};
    material.r = entry.r;
    material.g = entry.g;
    material.b = entry.b;
    material.roughness = entry.roughness;
    material.metallic = entry.metallic;
    material.emission = entry.emission;
    material.ior = entry.ior;
    material.transmission = entry.transmission;
    material.thin = entry.thin;
    material.subsurface = entry.subsurface;
    return material;
}

MaterialGpu defaultMaterialGpu()
{
    MaterialGpu material{};
    material.r = 0.8f;
    material.g = 0.8f;
    material.b = 0.8f;
    material.roughness = 0.5f;
    material.metallic = 0.0f;
    material.emission = 0.0f;
    material.ior = 1.5f;
    material.transmission = 0.0f;
    material.thin = 0.0f;
    material.subsurface = 0.0f;
    return material;
}

void buildMaterialTable(
    const std::vector<MaterialDefinition>& definitions,
    std::vector<MaterialGpu>& outMaterials,
    std::unordered_map<std::string, uint32_t>& outLSystemIdToIndex)
{
    outMaterials.clear();
    outLSystemIdToIndex.clear();

    for (const MaterialDefinition& definition : definitions) {
        if (outLSystemIdToIndex.find(definition.id) != outLSystemIdToIndex.end()) {
            continue;
        }

        const uint32_t index = static_cast<uint32_t>(outMaterials.size());
        outLSystemIdToIndex.emplace(definition.id, index);
        outMaterials.push_back(toMaterialGpu(definition.entry));
    }

    if (outLSystemIdToIndex.find("0") == outLSystemIdToIndex.end()) {
        const uint32_t index = static_cast<uint32_t>(outMaterials.size());
        outLSystemIdToIndex.emplace("0", index);
        outMaterials.push_back(defaultMaterialGpu());
    }
}

uint32_t remapMaterialId(
    const std::unordered_map<std::string, uint32_t>& table,
    const std::string& lsystemId)
{
    const auto it = table.find(lsystemId);
    if (it != table.end()) {
        return it->second;
    }

    const auto fallback = table.find("0");
    if (fallback != table.end()) {
        return fallback->second;
    }

    return 0;
}

void assignMaterialIndex(HostMesh& mesh, const uint32_t materialIndex)
{
    for (HostTriangle& tri : mesh.triangles) {
        tri.materialIndex = materialIndex;
    }
}

} // namespace

bool ProceduralMeshBuilder::buildHostMesh(
    std::string_view definition,
    const std::size_t iterations,
    const RootTransform& root,
    HostMesh& outMesh,
    const ProceduralBuildParams& params)
{
    outMesh.triangles.clear();
    outMesh.materials.clear();

    const LSystemEvaluationResult eval =
        LSystemEvaluator::evaluate(std::string(definition), iterations);
    const TurtleOutput turtleOutput = turtleExecute(eval.generation, params.turtle);
    if (turtleOutput.segments.empty()) {
        return false;
    }

    std::unordered_map<std::string, uint32_t> lsystemIdToIndex;
    buildMaterialTable(eval.materials, outMesh.materials, lsystemIdToIndex);

    for (const TurtleSegment& segment : turtleOutput.segments) {
        manifold::Manifold segmentMesh = loftOrSphereFromSegment(segment, params);
        if (!isValidManifold(segmentMesh)) {
            continue;
        }

        segmentMesh = applyRootTransform(segmentMesh, root);
        if (!isValidManifold(segmentMesh)) {
            continue;
        }

        segmentMesh = segmentMesh.CalculateNormals(0, static_cast<double>(params.creaseAngleDeg));
        if (!isValidManifold(segmentMesh)) {
            continue;
        }

        HostMesh piece = meshFromManifold(segmentMesh);
        if (piece.triangles.empty()) {
            continue;
        }

        const uint32_t materialIndex = remapMaterialId(lsystemIdToIndex, segment.materialId);
        assignMaterialIndex(piece, materialIndex);
        hostMeshAppend(outMesh, piece);
    }

    return !outMesh.triangles.empty();
}
