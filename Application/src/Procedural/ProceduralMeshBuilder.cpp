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

void ensureTextureBankBase(std::vector<TextureDescGpu>& bank)
{
    if (bank.empty()) {
        TextureDescGpu placeholder{};
        placeholder.kind = static_cast<uint32_t>(TextureKind::ConstantScalar);
        bank.push_back(placeholder);
    }
}

TextureDescGpu packGridTexture(const TextureDef& texture)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Grid2D);
    const float defaultFrequency = 8.0f;
    const float defaultThickness = 0.05f;
    float freqU = defaultFrequency;
    float freqV = defaultFrequency;
    float thickness = defaultThickness;

    if (texture.params.size() == 7u) {
        freqU = texture.params[6];
        freqV = texture.params[6];
    } else if (texture.params.size() == 8u) {
        freqU = texture.params[6];
        freqV = texture.params[6];
        thickness = texture.params[7];
    } else if (texture.params.size() == 9u) {
        freqU = texture.params[6];
        freqV = texture.params[7];
        thickness = texture.params[8];
    } else if (texture.params.size() >= 10u) {
        freqU = texture.params[6];
        freqV = texture.params[7];
        thickness = texture.params[8];
    }

    desc.p0 = make_float4(freqU, freqV, thickness, 0.0f);
    desc.p1 = make_float4(texture.params[0], texture.params[1], texture.params[2], 0.0f);
    desc.p2 = make_float4(texture.params[3], texture.params[4], texture.params[5], 0.0f);
    return desc;
}

TextureDescGpu packStripeTexture(const TextureDef& texture)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Stripe1D);
    const float onValue = texture.params.size() > 2u ? texture.params[2] : 1.0f;
    const float offValue = texture.params.size() > 3u ? texture.params[3] : 0.0f;
    desc.p0 = make_float4(texture.params[0], texture.params[1], onValue, offValue);
    return desc;
}

TextureDescGpu packTextureDesc(const TextureDef& texture)
{
    if (texture.kind == "Grid") {
        return packGridTexture(texture);
    }
    return packStripeTexture(texture);
}

uint32_t addTexture(std::vector<TextureDescGpu>& bank, const TextureDef& texture)
{
    ensureTextureBankBase(bank);
    bank.push_back(packTextureDesc(texture));
    return static_cast<uint32_t>(bank.size() - 1u);
}

uint32_t channelTextureIndex(const MaterialChannel& channel, std::vector<TextureDescGpu>& bank)
{
    if (channel.mode != MaterialChannel::Mode::Texture) {
        return 0u;
    }
    return addTexture(bank, channel.texture);
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
    material.ior = materialChannelScalar(entry.ior, 1.5f);
    material.transmission = materialChannelScalar(entry.transmission);
    material.thin = materialChannelScalar(entry.thin);
    material.subsurface = materialChannelScalar(entry.subsurface);

    material.albedoTex = channelTextureIndex(entry.albedo, bank);
    material.roughnessTex = channelTextureIndex(entry.roughness, bank);
    material.metallicTex = channelTextureIndex(entry.metallic, bank);
    material.transmissionTex = channelTextureIndex(entry.transmission, bank);
    material.thinTex = channelTextureIndex(entry.thin, bank);
    material.iorTex = channelTextureIndex(entry.ior, bank);
    material.subsurfaceTex = channelTextureIndex(entry.subsurface, bank);
    material.emissionTex = channelTextureIndex(entry.emission, bank);
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
    outMesh.textures.clear();

    const LSystemEvaluationResult eval =
        LSystemEvaluator::evaluate(std::string(definition), iterations);
    const TurtleOutput turtleOutput = turtleExecute(eval.generation, params.turtle);
    if (turtleOutput.segments.empty()) {
        return false;
    }

    std::unordered_map<std::string, uint32_t> lsystemIdToIndex;
    buildMaterialTable(eval.materials, outMesh.materials, outMesh.textures, lsystemIdToIndex);

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
