#include "MeshAccelGltfIO.h"

#include "MeshAccelMaterialJson.h"
#include "MeshAccelTextureBake.h"
#include "Medium/MediumProperties.h"
#include "SceneUnits.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <vector>

namespace {

constexpr float kMmToMeter = 0.001f;
constexpr int kDefaultBakeResolution = 512;
constexpr uint32_t kGlbMagic = 0x46546C67u;
constexpr uint32_t kGlbVersion = 2u;
constexpr uint32_t kChunkJson = 0x4E4F534Au;
constexpr uint32_t kChunkBin = 0x004E4942u;
constexpr int kComponentFloat = 5126;
constexpr int kComponentUInt = 5125;
constexpr int kTargetArrayBuffer = 34962;
constexpr int kTargetElementArrayBuffer = 34963;

size_t align4(size_t value)
{
    return (value + 3u) & ~size_t{3u};
}

class GlbBuilder
{
public:
    int addBufferView(const void* data, size_t byteLength, int target = 0)
    {
        const size_t offset = align4(m_bin.size());
        m_bin.resize(offset);
        const auto* bytes = static_cast<const uint8_t*>(data);
        m_bin.insert(m_bin.end(), bytes, bytes + byteLength);

        QJsonObject view;
        view.insert(QStringLiteral("buffer"), 0);
        view.insert(QStringLiteral("byteOffset"), static_cast<int>(offset));
        view.insert(QStringLiteral("byteLength"), static_cast<int>(byteLength));
        if (target != 0) {
            view.insert(QStringLiteral("target"), target);
        }
        const int index = static_cast<int>(m_bufferViews.size());
        m_bufferViews.append(view);
        return index;
    }

    int addAccessor(
        int bufferViewIndex,
        int componentType,
        const QString& type,
        int count,
        const QJsonArray& maxArr = {},
        const QJsonArray& minArr = {})
    {
        QJsonObject accessor;
        accessor.insert(QStringLiteral("bufferView"), bufferViewIndex);
        accessor.insert(QStringLiteral("componentType"), componentType);
        accessor.insert(QStringLiteral("type"), type);
        accessor.insert(QStringLiteral("count"), count);
        if (!maxArr.isEmpty()) {
            accessor.insert(QStringLiteral("max"), maxArr);
        }
        if (!minArr.isEmpty()) {
            accessor.insert(QStringLiteral("min"), minArr);
        }
        const int index = static_cast<int>(m_accessors.size());
        m_accessors.append(accessor);
        return index;
    }

    int addImagePng(const std::vector<uint8_t>& pngBytes)
    {
        const int bufferViewIndex = addBufferView(pngBytes.data(), pngBytes.size());
        QJsonObject image;
        image.insert(QStringLiteral("bufferView"), bufferViewIndex);
        image.insert(QStringLiteral("mimeType"), QStringLiteral("image/png"));
        const int index = static_cast<int>(m_images.size());
        m_images.append(image);
        return index;
    }

    int addTexture(int imageIndex)
    {
        QJsonObject texture;
        texture.insert(QStringLiteral("source"), imageIndex);
        const int index = static_cast<int>(m_textures.size());
        m_textures.append(texture);
        return index;
    }

    size_t binByteLength() const { return m_bin.size(); }

    std::vector<uint8_t> buildGlb(const QJsonObject& rootJson) const
    {
        const QByteArray jsonBytes = QJsonDocument(rootJson).toJson(QJsonDocument::Compact);
        std::vector<uint8_t> jsonChunk(jsonBytes.begin(), jsonBytes.end());
        jsonChunk.resize(align4(jsonChunk.size()), ' ');

        std::vector<uint8_t> binChunk = m_bin;
        binChunk.resize(align4(binChunk.size()), 0);

        const uint32_t totalLength = static_cast<uint32_t>(12 + 8 + jsonChunk.size() + 8 + binChunk.size());

        std::vector<uint8_t> glb;
        glb.reserve(totalLength);

        auto appendU32 = [&glb](uint32_t value) {
            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
            glb.insert(glb.end(), ptr, ptr + 4);
        };

        appendU32(kGlbMagic);
        appendU32(kGlbVersion);
        appendU32(totalLength);

        appendU32(static_cast<uint32_t>(jsonChunk.size()));
        appendU32(kChunkJson);
        glb.insert(glb.end(), jsonChunk.begin(), jsonChunk.end());

        appendU32(static_cast<uint32_t>(binChunk.size()));
        appendU32(kChunkBin);
        glb.insert(glb.end(), binChunk.begin(), binChunk.end());

        return glb;
    }

    QJsonArray bufferViews() const { return m_bufferViews; }
    QJsonArray accessors() const { return m_accessors; }
    QJsonArray images() const { return m_images; }
    QJsonArray textures() const { return m_textures; }

private:
    std::vector<uint8_t> m_bin;
    QJsonArray m_bufferViews;
    QJsonArray m_accessors;
    QJsonArray m_images;
    QJsonArray m_textures;
};

struct PrimitiveBuild
{
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<uint32_t> indices;
};

void appendTriangle(const TriangleGpu& tri, PrimitiveBuild& build)
{
    const uint32_t baseIndex = static_cast<uint32_t>(build.positions.size() / 3);
    const Vec3 verts[3] = {tri.v0, tri.v1, tri.v2};
    const Vec3 norms[3] = {tri.n0, tri.n1, tri.n2};
    const Vec2 uvs[3] = {tri.uv0, tri.uv1, tri.uv2};

    for (int i = 0; i < 3; ++i) {
        build.positions.push_back(verts[i].x * kMmToMeter);
        build.positions.push_back(verts[i].y * kMmToMeter);
        build.positions.push_back(verts[i].z * kMmToMeter);
        build.normals.push_back(norms[i].x);
        build.normals.push_back(norms[i].y);
        build.normals.push_back(norms[i].z);
        build.uvs.push_back(uvs[i].x);
        build.uvs.push_back(uvs[i].y);
        build.indices.push_back(baseIndex + static_cast<uint32_t>(i));
    }
}

void computePositionBounds(const std::vector<float>& positions, QJsonArray* outMin, QJsonArray* outMax)
{
    if (outMin == nullptr || outMax == nullptr || positions.size() < 3) {
        return;
    }
    float minX = positions[0];
    float minY = positions[1];
    float minZ = positions[2];
    float maxX = positions[0];
    float maxY = positions[1];
    float maxZ = positions[2];
    for (size_t i = 3; i + 2 < positions.size(); i += 3) {
        minX = std::min(minX, positions[i]);
        minY = std::min(minY, positions[i + 1]);
        minZ = std::min(minZ, positions[i + 2]);
        maxX = std::max(maxX, positions[i]);
        maxY = std::max(maxY, positions[i + 1]);
        maxZ = std::max(maxZ, positions[i + 2]);
    }
    *outMin = QJsonArray{static_cast<double>(minX), static_cast<double>(minY), static_cast<double>(minZ)};
    *outMax = QJsonArray{static_cast<double>(maxX), static_cast<double>(maxY), static_cast<double>(maxZ)};
}

void appendExtensionUsed(QJsonArray& extensionsUsed, const QString& name)
{
    for (const QJsonValue& used : extensionsUsed) {
        if (used.toString() == name) {
            return;
        }
    }
    extensionsUsed.append(name);
}

QJsonObject buildGltfMaterialJson(
    uint32_t materialIndex,
    const MaterialGpu& material,
    const std::vector<TriangleGpu>& triangles,
    const std::vector<TextureDescGpu>& textures,
    GlbBuilder& builder,
    QJsonArray& extensionsUsed)
{
    QJsonObject pbr;
    pbr.insert(QStringLiteral("baseColorFactor"), QJsonArray{
        static_cast<double>(material.r),
        static_cast<double>(material.g),
        static_cast<double>(material.b),
        1.0});
    pbr.insert(QStringLiteral("metallicFactor"), static_cast<double>(material.metallic));
    pbr.insert(QStringLiteral("roughnessFactor"), static_cast<double>(material.roughness));

    auto maybeBakeTexture = [&](MaterialBakeChannel channel, const char* slotKey) {
        if (!materialChannelNeedsBake(material, channel)) {
            return;
        }
        BakedImage image{};
        if (!bakeMaterialChannel(triangles, materialIndex, material, textures, channel, kDefaultBakeResolution, &image)) {
            return;
        }
        std::vector<uint8_t> pngBytes;
        if (!encodePngFromRgba(image, &pngBytes)) {
            return;
        }
        const int imageIndex = builder.addImagePng(pngBytes);
        const int textureIndex = builder.addTexture(imageIndex);
        QJsonObject texInfo;
        texInfo.insert(QStringLiteral("index"), textureIndex);
        pbr.insert(QString::fromLatin1(slotKey), texInfo);
    };

    maybeBakeTexture(MaterialBakeChannel::AlbedoRgb, "baseColorTexture");
    maybeBakeTexture(MaterialBakeChannel::Roughness, "roughnessTexture");
    maybeBakeTexture(MaterialBakeChannel::Metallic, "metallicTexture");

    QJsonObject matJson;
    matJson.insert(QStringLiteral("name"), QStringLiteral("mat_%1").arg(materialIndex));
    matJson.insert(QStringLiteral("pbrMetallicRoughness"), pbr);

    QJsonObject extensions;

    if (material.emission > 0.0f) {
        matJson.insert(QStringLiteral("emissiveFactor"), QJsonArray{
            static_cast<double>(material.r),
            static_cast<double>(material.g),
            static_cast<double>(material.b)});
        extensions.insert(QStringLiteral("KHR_materials_emissive_strength"), QJsonObject{
            {QStringLiteral("emissiveStrength"), static_cast<double>(material.emission)}});
        appendExtensionUsed(extensionsUsed, QStringLiteral("KHR_materials_emissive_strength"));
    }

    extensions.insert(QStringLiteral("KHR_materials_ior"), QJsonObject{
        {QStringLiteral("ior"), static_cast<double>(material.ior)}});
    appendExtensionUsed(extensionsUsed, QStringLiteral("KHR_materials_ior"));

    if (materialIsClearMedium(material)) {
        extensions.insert(QStringLiteral("KHR_materials_transmission"), QJsonObject{
            {QStringLiteral("transmissionFactor"), 1.0}});
        matJson.insert(QStringLiteral("alphaMode"), QStringLiteral("BLEND"));
        appendExtensionUsed(extensionsUsed, QStringLiteral("KHR_materials_transmission"));
    }

    if (!extensions.isEmpty()) {
        matJson.insert(QStringLiteral("extensions"), extensions);
    }

    matJson.insert(QStringLiteral("extras"), QJsonObject{
        {QStringLiteral("pathTracer"), pathTracerMaterialExtrasJson(material)}});

    return matJson;
}

} // namespace

bool exportMeshGltf(
    const std::vector<TriangleGpu>& triangles,
    const std::vector<MaterialGpu>& materials,
    const std::vector<TextureDescGpu>& textures,
    const QString& glbFilePath,
    QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (triangles.empty()) {
        return fail(QStringLiteral("Scene has no geometry to export."));
    }

    const QFileInfo fileInfo(glbFilePath);
    if (!fileInfo.dir().exists()) {
        return fail(QStringLiteral("Output directory does not exist."));
    }

    GlbBuilder builder;
    QJsonArray gltfMaterials;
    QJsonArray extensionsUsed;

    const size_t materialCount = materials.empty() ? 1 : materials.size();
    if (materials.empty()) {
        MaterialGpu defaultMaterial{};
        gltfMaterials.append(buildGltfMaterialJson(0, defaultMaterial, triangles, textures, builder, extensionsUsed));
    } else {
        for (uint32_t materialIndex = 0; materialIndex < materials.size(); ++materialIndex) {
            gltfMaterials.append(
                buildGltfMaterialJson(materialIndex, materials[materialIndex], triangles, textures, builder, extensionsUsed));
        }
    }

    std::vector<PrimitiveBuild> primitiveBuilds(materialCount);
    for (const TriangleGpu& tri : triangles) {
        const uint32_t materialIndex = materials.empty() ? 0u : tri.materialIndex;
        if (materialIndex >= primitiveBuilds.size()) {
            return fail(QStringLiteral("Triangle references invalid material index."));
        }
        appendTriangle(tri, primitiveBuilds[materialIndex]);
    }

    QJsonArray meshPrimitives;
    for (size_t materialIndex = 0; materialIndex < primitiveBuilds.size(); ++materialIndex) {
        const PrimitiveBuild& build = primitiveBuilds[materialIndex];
        if (build.indices.empty()) {
            continue;
        }

        QJsonArray minPos;
        QJsonArray maxPos;
        computePositionBounds(build.positions, &minPos, &maxPos);

        const int posView = builder.addBufferView(build.positions.data(), build.positions.size() * sizeof(float), kTargetArrayBuffer);
        const int posAccessor = builder.addAccessor(
            posView, kComponentFloat, QStringLiteral("VEC3"), static_cast<int>(build.positions.size() / 3), maxPos, minPos);

        const int normView = builder.addBufferView(build.normals.data(), build.normals.size() * sizeof(float), kTargetArrayBuffer);
        const int normAccessor = builder.addAccessor(
            normView, kComponentFloat, QStringLiteral("VEC3"), static_cast<int>(build.normals.size() / 3));

        const int uvView = builder.addBufferView(build.uvs.data(), build.uvs.size() * sizeof(float), kTargetArrayBuffer);
        const int uvAccessor = builder.addAccessor(
            uvView, kComponentFloat, QStringLiteral("VEC2"), static_cast<int>(build.uvs.size() / 2));

        const int indexView = builder.addBufferView(build.indices.data(), build.indices.size() * sizeof(uint32_t), kTargetElementArrayBuffer);
        const int indexAccessor = builder.addAccessor(
            indexView, kComponentUInt, QStringLiteral("SCALAR"), static_cast<int>(build.indices.size()));

        QJsonObject attributes;
        attributes.insert(QStringLiteral("POSITION"), posAccessor);
        attributes.insert(QStringLiteral("NORMAL"), normAccessor);
        attributes.insert(QStringLiteral("TEXCOORD_0"), uvAccessor);

        QJsonObject primitive;
        primitive.insert(QStringLiteral("attributes"), attributes);
        primitive.insert(QStringLiteral("indices"), indexAccessor);
        primitive.insert(QStringLiteral("material"), static_cast<int>(materialIndex));
        meshPrimitives.append(primitive);
    }

    if (meshPrimitives.isEmpty()) {
        return fail(QStringLiteral("No mesh primitives to export."));
    }

    QJsonObject root;
    root.insert(QStringLiteral("asset"), QJsonObject{
        {QStringLiteral("version"), QStringLiteral("2.0")},
        {QStringLiteral("generator"), QStringLiteral("PathTracer")}});
    root.insert(QStringLiteral("extras"), QJsonObject{
        {QStringLiteral("pathTracer"), QJsonObject{
            {QStringLiteral("units"), QStringLiteral("millimeters")},
            {QStringLiteral("positionScale"), static_cast<double>(kMmToMeter)}}}});
    if (!extensionsUsed.isEmpty()) {
        root.insert(QStringLiteral("extensionsUsed"), extensionsUsed);
    }

    root.insert(QStringLiteral("buffers"), QJsonArray{
        QJsonObject{{QStringLiteral("byteLength"), static_cast<int>(builder.binByteLength())}}});
    root.insert(QStringLiteral("bufferViews"), builder.bufferViews());
    root.insert(QStringLiteral("accessors"), builder.accessors());
    if (!builder.images().isEmpty()) {
        root.insert(QStringLiteral("images"), builder.images());
    }
    if (!builder.textures().isEmpty()) {
        root.insert(QStringLiteral("textures"), builder.textures());
    }
    root.insert(QStringLiteral("materials"), gltfMaterials);
    root.insert(QStringLiteral("meshes"), QJsonArray{
        QJsonObject{
            {QStringLiteral("name"), QStringLiteral("scene")},
            {QStringLiteral("primitives"), meshPrimitives}}});
    root.insert(QStringLiteral("nodes"), QJsonArray{
        QJsonObject{{QStringLiteral("mesh"), 0}}});
    root.insert(QStringLiteral("scenes"), QJsonArray{
        QJsonObject{{QStringLiteral("nodes"), QJsonArray{0}}}});
    root.insert(QStringLiteral("scene"), 0);

    const std::vector<uint8_t> glbBytes = builder.buildGlb(root);

    QFile file(glbFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(QStringLiteral("Failed to open GLB file for writing: %1").arg(glbFilePath));
    }
    file.write(reinterpret_cast<const char*>(glbBytes.data()), static_cast<qint64>(glbBytes.size()));
    return true;
}
