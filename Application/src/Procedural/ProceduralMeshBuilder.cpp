#include "ProceduralMeshBuilder.h"

#include "Geometry/MathCore.h"
#include "LSystemEvaluator.h"
#include "LSystemMaterials.h"
#include "Loft.h"
#include "MeshAccel/MaterialType.h"
#include "Texture/TexturePack.h"
#include "Turtle.h"

#include <cmath>
#include <exception>
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

void ensureTextureBankBase(std::vector<TextureDescGpu>& bank)
{
    if (bank.empty()) {
        TextureDescGpu placeholder{};
        placeholder.kind = static_cast<uint32_t>(TextureKind::ConstantScalar);
        bank.push_back(placeholder);
    }
}

TextureDescGpu packTextureDescLocal(const TextureDef& texture)
{
    return ::packTextureDesc(
        texture.kind.c_str(),
        texture.params.data(),
        texture.params.size());
}

uint32_t addTexture(std::vector<TextureDescGpu>& bank, const TextureDef& texture)
{
    ensureTextureBankBase(bank);
    bank.push_back(packTextureDescLocal(texture));
    return static_cast<uint32_t>(bank.size() - 1u);
}

uint32_t channelTextureIndex(const MaterialChannel& channel, std::vector<TextureDescGpu>& bank)
{
    if (channel.mode != MaterialChannel::Mode::Texture) {
        return 0u;
    }
    return addTexture(bank, channel.texture);
}

uint32_t materialTypeFromName(const std::string& typeName)
{
    if (typeName == "Glass") {
        return static_cast<uint32_t>(MaterialType::Glass);
    }
    if (typeName == "Subsurface") {
        return static_cast<uint32_t>(MaterialType::Subsurface);
    }
    if (typeName == "Emissive") {
        return static_cast<uint32_t>(MaterialType::Emissive);
    }
    return static_cast<uint32_t>(MaterialType::Opaque);
}

MaterialGpu toMaterialGpu(const MaterialEntry& entry, std::vector<TextureDescGpu>& bank)
{
    MaterialGpu material{};
    material.r = materialChannelR(entry.albedo);
    material.g = materialChannelG(entry.albedo);
    material.b = materialChannelB(entry.albedo);
    material.roughness = materialChannelScalar(entry.roughness, 0.5f);
    material.metallic = materialChannelScalar(entry.metallic);
    material.emission = materialChannelScalar(entry.emission);
    material.diffuseRoughness = materialChannelScalar(entry.diffuseRoughness, -1.0f);
    material.specular = materialChannelScalar(entry.specular, 1.0f);
    material.materialType = materialTypeFromName(entry.typeName);
    material.subsurface = materialChannelScalar(entry.subsurface, 0.0f);
    material.subsurfaceRadiusR = materialChannelR(entry.subsurfaceRadius, 1.0f);
    material.subsurfaceRadiusG = materialChannelG(entry.subsurfaceRadius, 1.0f);
    material.subsurfaceRadiusB = materialChannelB(entry.subsurfaceRadius, 1.0f);
    material.ior = materialChannelScalar(entry.ior, 1.5f);
    material.abbeNumber = materialChannelScalar(entry.abbe, 58.0f);

    material.albedoTex = channelTextureIndex(entry.albedo, bank);
    material.roughnessTex = channelTextureIndex(entry.roughness, bank);
    material.metallicTex = channelTextureIndex(entry.metallic, bank);
    material.emissionTex = channelTextureIndex(entry.emission, bank);
    material.diffuseRoughnessTex = channelTextureIndex(entry.diffuseRoughness, bank);
    material.specularTex = channelTextureIndex(entry.specular, bank);
    material.iorTex = channelTextureIndex(entry.ior, bank);
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
    material.abbeNumber = 58.0f;
    material.diffuseRoughness = -1.0f;
    material.specular = 1.0f;
    material.materialType = static_cast<uint32_t>(MaterialType::Opaque);
    return material;
}

void buildMaterialTable(
    const std::vector<MaterialDefinition>& definitions,
    std::vector<MaterialGpu>& outMaterials,
    std::vector<TextureDescGpu>& outTextures,
    std::unordered_map<std::string, uint32_t>& outLSystemIdToIndex)
{
    outMaterials.clear();
    outTextures.clear();
    outLSystemIdToIndex.clear();

    for (const MaterialDefinition& definition : definitions) {
        if (outLSystemIdToIndex.find(definition.id) != outLSystemIdToIndex.end()) {
            continue;
        }

        const uint32_t index = static_cast<uint32_t>(outMaterials.size());
        outLSystemIdToIndex.emplace(definition.id, index);
        outMaterials.push_back(toMaterialGpu(definition.entry, outTextures));
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

void assignMaterialIndex(Mesh& mesh, const uint32_t materialIndex)
{
    for (MeshTriangle& tri : mesh.triangles) {
        tri.materialIndex = materialIndex;
    }
}

void applyRootTransformToMesh(Mesh& mesh, const RootTransform& root)
{
    for (MeshTriangle& tri : mesh.triangles) {
        tri.v0 = vecAdd3(
            vecRotateYawPitchRoll(tri.v0, root.rotationDeg.x, root.rotationDeg.y, root.rotationDeg.z),
            root.translation);
        tri.v1 = vecAdd3(
            vecRotateYawPitchRoll(tri.v1, root.rotationDeg.x, root.rotationDeg.y, root.rotationDeg.z),
            root.translation);
        tri.v2 = vecAdd3(
            vecRotateYawPitchRoll(tri.v2, root.rotationDeg.x, root.rotationDeg.y, root.rotationDeg.z),
            root.translation);
        tri.n0 = vecNormalize3(
            vecRotateYawPitchRoll(tri.n0, root.rotationDeg.x, root.rotationDeg.y, root.rotationDeg.z));
        tri.n1 = vecNormalize3(
            vecRotateYawPitchRoll(tri.n1, root.rotationDeg.x, root.rotationDeg.y, root.rotationDeg.z));
        tri.n2 = vecNormalize3(
            vecRotateYawPitchRoll(tri.n2, root.rotationDeg.x, root.rotationDeg.y, root.rotationDeg.z));
    }
}

} // namespace

void ProceduralMeshBuilder::applyRootTransform(Mesh& mesh, const RootTransform& root)
{
    applyRootTransformToMesh(mesh, root);
}

bool ProceduralMeshBuilder::buildHostMesh(
    std::string_view definition,
    const std::size_t iterations,
    const RootTransform& root,
    Mesh& outMesh,
    const ProceduralBuildParams& params,
    std::string* outError)
{
    outMesh.triangles.clear();
    outMesh.materials.clear();
    outMesh.textures.clear();

    LSystemEvaluationResult eval;
    try {
        eval = LSystemEvaluator::evaluate(std::string(definition), iterations);
    } catch (const std::exception& e) {
        if (outError != nullptr) {
            *outError = e.what();
        }
        return false;
    }
    const TurtleOutput turtleOutput = turtleExecute(eval.generation, params.turtle);
    if (turtleOutput.segments.empty()) {
        return false;
    }

    std::unordered_map<std::string, uint32_t> lsystemIdToIndex;
    buildMaterialTable(eval.materials, outMesh.materials, outMesh.textures, lsystemIdToIndex);

    for (const TurtleSegment& segment : turtleOutput.segments) {
        Mesh piece = renderMeshFromSegment(segment, params);
        if (piece.triangles.empty()) {
            continue;
        }

        applyRootTransformToMesh(piece, root);

        const uint32_t materialIndex = remapMaterialId(lsystemIdToIndex, segment.materialId);
        assignMaterialIndex(piece, materialIndex);
        meshAppend(outMesh, piece);
    }

    return !outMesh.triangles.empty();
}
