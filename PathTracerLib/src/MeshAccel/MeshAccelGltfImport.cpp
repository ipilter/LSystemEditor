#include "MeshAccelGltfIO.h"

#include "MeshAccel/MaterialType.h"
#include "MeshAccelMaterialJson.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

constexpr float kMeterToMm = 1000.0f;
constexpr uint32_t kGlbMagic = 0x46546C67u;
constexpr int kComponentFloat = 5126;
constexpr int kComponentUInt = 5125;
constexpr int kComponentUShort = 5123;

struct GltfLoaded
{
    QJsonObject root;
    std::vector<uint8_t> bin;
};

bool readGlbFile(const QString& path, GltfLoaded* out, QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (out == nullptr) {
        return fail(QStringLiteral("Output pointer is null."));
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("Failed to open glTF file: %1").arg(path));
    }

    const QByteArray data = file.readAll();
    if (data.size() < 20) {
        return fail(QStringLiteral("File is too small to be a valid GLB."));
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t length = 0;
    std::memcpy(&magic, bytes, 4);
    std::memcpy(&version, bytes + 4, 4);
    std::memcpy(&length, bytes + 8, 4);

    if (magic != kGlbMagic || version != 2u) {
        return fail(QStringLiteral("Unsupported glTF container (expected GLB v2)."));
    }

    size_t offset = 12;
    QJsonObject root;
    out->bin.clear();

    while (offset + 8 <= static_cast<size_t>(data.size()) && offset < length) {
        uint32_t chunkLength = 0;
        uint32_t chunkType = 0;
        std::memcpy(&chunkLength, bytes + offset, 4);
        std::memcpy(&chunkType, bytes + offset + 4, 4);
        offset += 8;

        if (offset + chunkLength > static_cast<size_t>(data.size())) {
            return fail(QStringLiteral("GLB chunk exceeds file size."));
        }

        if (chunkType == 0x4E4F534Au) {
            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(
                QByteArray(reinterpret_cast<const char*>(bytes + offset), static_cast<int>(chunkLength)),
                &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                return fail(QStringLiteral("Invalid glTF JSON chunk: %1").arg(parseError.errorString()));
            }
            root = doc.object();
        } else if (chunkType == 0x004E4942u) {
            out->bin.assign(bytes + offset, bytes + offset + chunkLength);
        }

        offset += chunkLength;
    }

    if (root.isEmpty()) {
        return fail(QStringLiteral("GLB contains no JSON chunk."));
    }

    out->root = root;
    return true;
}

bool readSeparateGltfFile(const QString& path, GltfLoaded* out, QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    QFile jsonFile(path);
    if (!jsonFile.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("Failed to open glTF file: %1").arg(path));
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(QStringLiteral("Invalid glTF JSON: %1").arg(parseError.errorString()));
    }

    out->root = doc.object();
    out->bin.clear();

    const QJsonArray buffers = out->root.value(QStringLiteral("buffers")).toArray();
    if (buffers.isEmpty()) {
        return fail(QStringLiteral("glTF has no buffers."));
    }

    const QJsonObject buffer0 = buffers.at(0).toObject();
    const QString uri = buffer0.value(QStringLiteral("uri")).toString();
    if (uri.isEmpty()) {
        return fail(QStringLiteral("Separate .gltf buffer must use a .bin uri."));
    }

    const QFileInfo gltfInfo(path);
    const QString binPath = gltfInfo.dir().filePath(uri);
    QFile binFile(binPath);
    if (!binFile.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("Failed to open glTF binary: %1").arg(binPath));
    }

    const QByteArray binData = binFile.readAll();
    out->bin.assign(binData.begin(), binData.end());
    return true;
}

const uint8_t* bufferSlice(
    const GltfLoaded& gltf,
    const QJsonObject& bufferView,
    size_t* outByteLength)
{
    const int byteOffset = bufferView.value(QStringLiteral("byteOffset")).toInt(0);
    const int byteLength = bufferView.value(QStringLiteral("byteLength")).toInt(0);
    if (byteOffset < 0 || byteLength <= 0 || static_cast<size_t>(byteOffset + byteLength) > gltf.bin.size()) {
        return nullptr;
    }
    if (outByteLength != nullptr) {
        *outByteLength = static_cast<size_t>(byteLength);
    }
    return gltf.bin.data() + byteOffset;
}

template<typename T>
bool readAccessor(
    const GltfLoaded& gltf,
    int accessorIndex,
    std::vector<T>* outValues,
    QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    const QJsonArray accessors = gltf.root.value(QStringLiteral("accessors")).toArray();
    if (accessorIndex < 0 || accessorIndex >= accessors.size()) {
        return fail(QStringLiteral("Invalid accessor index."));
    }

    const QJsonObject accessor = accessors.at(accessorIndex).toObject();
    const int bufferViewIndex = accessor.value(QStringLiteral("bufferView")).toInt(-1);
    const int componentType = accessor.value(QStringLiteral("componentType")).toInt();
    const QString type = accessor.value(QStringLiteral("type")).toString();
    const int count = accessor.value(QStringLiteral("count")).toInt(0);
    const int byteOffset = accessor.value(QStringLiteral("byteOffset")).toInt(0);

    const QJsonArray bufferViews = gltf.root.value(QStringLiteral("bufferViews")).toArray();
    if (bufferViewIndex < 0 || bufferViewIndex >= bufferViews.size()) {
        return fail(QStringLiteral("Invalid bufferView index."));
    }

    size_t viewLength = 0;
    const uint8_t* viewData = bufferSlice(gltf, bufferViews.at(bufferViewIndex).toObject(), &viewLength);
    if (viewData == nullptr || static_cast<size_t>(byteOffset) >= viewLength) {
        return fail(QStringLiteral("Accessor bufferView out of range."));
    }
    viewData += byteOffset;
    viewLength -= static_cast<size_t>(byteOffset);

    outValues->clear();

    if (type == QStringLiteral("VEC3") && componentType == kComponentFloat && sizeof(T) == sizeof(float) * 3) {
        const size_t needed = static_cast<size_t>(count) * 3 * sizeof(float);
        if (viewLength < needed) {
            return fail(QStringLiteral("VEC3 accessor exceeds bufferView."));
        }
        outValues->resize(static_cast<size_t>(count));
        std::memcpy(outValues->data(), viewData, needed);
        return true;
    }

    if (type == QStringLiteral("VEC2") && componentType == kComponentFloat && sizeof(T) == sizeof(float) * 2) {
        const size_t needed = static_cast<size_t>(count) * 2 * sizeof(float);
        if (viewLength < needed) {
            return fail(QStringLiteral("VEC2 accessor exceeds bufferView."));
        }
        outValues->resize(static_cast<size_t>(count));
        std::memcpy(outValues->data(), viewData, needed);
        return true;
    }

    if (type == QStringLiteral("SCALAR") && componentType == kComponentUInt && sizeof(T) == sizeof(uint32_t)) {
        const size_t needed = static_cast<size_t>(count) * sizeof(uint32_t);
        if (viewLength < needed) {
            return fail(QStringLiteral("UINT scalar accessor exceeds bufferView."));
        }
        outValues->resize(static_cast<size_t>(count));
        std::memcpy(outValues->data(), viewData, needed);
        return true;
    }

    if (type == QStringLiteral("SCALAR") && componentType == kComponentUShort && sizeof(T) == sizeof(uint16_t)) {
        const size_t needed = static_cast<size_t>(count) * sizeof(uint16_t);
        if (viewLength < needed) {
            return fail(QStringLiteral("USHORT scalar accessor exceeds bufferView."));
        }
        outValues->resize(static_cast<size_t>(count));
        std::memcpy(outValues->data(), viewData, needed);
        return true;
    }

    return fail(QStringLiteral("Unsupported accessor type/component combination."));
}

MaterialGpu materialFromGltfJson(const QJsonObject& matJson)
{
    MaterialGpu material{};

    const QJsonObject pbr = matJson.value(QStringLiteral("pbrMetallicRoughness")).toObject();
    const QJsonArray baseColor = pbr.value(QStringLiteral("baseColorFactor")).toArray();
    if (baseColor.size() >= 3) {
        material.r = static_cast<float>(baseColor.at(0).toDouble(0.8));
        material.g = static_cast<float>(baseColor.at(1).toDouble(0.8));
        material.b = static_cast<float>(baseColor.at(2).toDouble(0.8));
    }
    material.metallic = static_cast<float>(pbr.value(QStringLiteral("metallicFactor")).toDouble(0.0));
    material.roughness = static_cast<float>(pbr.value(QStringLiteral("roughnessFactor")).toDouble(0.5));

    const QJsonArray emissive = matJson.value(QStringLiteral("emissiveFactor")).toArray();
    float emissiveStrength = 0.0f;
    if (!emissive.isEmpty()) {
        emissiveStrength = 1.0f;
    }

    const QJsonObject extensions = matJson.value(QStringLiteral("extensions")).toObject();
    const QJsonObject emissiveStrengthExt = extensions.value(QStringLiteral("KHR_materials_emissive_strength")).toObject();
    if (!emissiveStrengthExt.isEmpty()) {
        material.emission = static_cast<float>(emissiveStrengthExt.value(QStringLiteral("emissiveStrength")).toDouble(1.0));
    } else if (emissiveStrength > 0.0f) {
        material.emission = emissiveStrength;
    }

    const QJsonObject iorExt = extensions.value(QStringLiteral("KHR_materials_ior")).toObject();
    if (!iorExt.isEmpty()) {
        material.ior = static_cast<float>(iorExt.value(QStringLiteral("ior")).toDouble(1.5));
    }

    applyPathTracerMaterialExtrasJson(matJson.value(QStringLiteral("extras")).toObject(), &material);
    applyLegacySubsurfaceInference(material);
    if (material.emission > 0.0f
        && material.materialType == static_cast<uint32_t>(MaterialType::Opaque)) {
        material.materialType = static_cast<uint32_t>(MaterialType::Emissive);
    }
    return material;
}

bool importPrimitive(
    const GltfLoaded& gltf,
    const QJsonObject& primitive,
    uint32_t materialIndex,
    std::vector<MeshTriangle>* outTriangles,
    QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    const QJsonObject attributes = primitive.value(QStringLiteral("attributes")).toObject();
    const int positionAccessor = attributes.value(QStringLiteral("POSITION")).toInt(-1);
    const int normalAccessor = attributes.value(QStringLiteral("NORMAL")).toInt(-1);
    const int uvAccessor = attributes.value(QStringLiteral("TEXCOORD_0")).toInt(-1);
    const int indicesAccessor = primitive.value(QStringLiteral("indices")).toInt(-1);

    if (positionAccessor < 0 || indicesAccessor < 0) {
        return fail(QStringLiteral("Primitive missing POSITION or indices accessor."));
    }

    struct Float3 { float x, y, z; };
    struct Float2 { float u, v; };

    std::vector<Float3> positions;
    std::vector<Float3> normals;
    std::vector<Float2> uvs;
    std::vector<uint32_t> indicesU32;
    std::vector<uint16_t> indicesU16;

    if (!readAccessor(gltf, positionAccessor, &positions, errorMessage)) {
        return fail(QStringLiteral("Failed to read POSITION accessor."));
    }

    if (normalAccessor >= 0) {
        if (!readAccessor(gltf, normalAccessor, &normals, errorMessage)) {
            return fail(QStringLiteral("Failed to read NORMAL accessor."));
        }
    }

    if (uvAccessor >= 0) {
        if (!readAccessor(gltf, uvAccessor, &uvs, errorMessage)) {
            return fail(QStringLiteral("Failed to read TEXCOORD accessor."));
        }
    }

    const QJsonArray accessors = gltf.root.value(QStringLiteral("accessors")).toArray();
    const int componentType = accessors.at(indicesAccessor).toObject().value(QStringLiteral("componentType")).toInt();
    if (componentType == kComponentUInt) {
        if (!readAccessor(gltf, indicesAccessor, &indicesU32, errorMessage)) {
            return fail(QStringLiteral("Failed to read indices accessor."));
        }
    } else if (componentType == kComponentUShort) {
        if (!readAccessor(gltf, indicesAccessor, &indicesU16, errorMessage)) {
            return fail(QStringLiteral("Failed to read indices accessor."));
        }
        indicesU32.resize(indicesU16.size());
        for (size_t i = 0; i < indicesU16.size(); ++i) {
            indicesU32[i] = indicesU16[i];
        }
    } else {
        return fail(QStringLiteral("Unsupported index component type."));
    }

    if (indicesU32.size() % 3 != 0) {
        return fail(QStringLiteral("Index count is not a multiple of 3."));
    }

    auto vertexAt = [&](uint32_t index, Vec3* pos, Vec3* normal, Vec2* uv) {
        if (index >= positions.size()) {
            return false;
        }
        pos->x = positions[index].x * kMeterToMm;
        pos->y = positions[index].y * kMeterToMm;
        pos->z = positions[index].z * kMeterToMm;

        if (normal != nullptr) {
            if (index < normals.size()) {
                *normal = Vec3{normals[index].x, normals[index].y, normals[index].z};
            } else {
                *normal = Vec3{0.0f, 1.0f, 0.0f};
            }
        }

        if (uv != nullptr) {
            if (index < uvs.size()) {
                *uv = Vec2{uvs[index].u, uvs[index].v};
            } else {
                *uv = Vec2{0.0f, 0.0f};
            }
        }
        return true;
    };

    for (size_t i = 0; i + 2 < indicesU32.size(); i += 3) {
        MeshTriangle tri{};
        tri.materialIndex = materialIndex;
        if (!vertexAt(indicesU32[i], &tri.v0, &tri.n0, &tri.uv0)
            || !vertexAt(indicesU32[i + 1], &tri.v1, &tri.n1, &tri.uv1)
            || !vertexAt(indicesU32[i + 2], &tri.v2, &tri.n2, &tri.uv2)) {
            return fail(QStringLiteral("Triangle references out-of-range vertex index."));
        }
        outTriangles->push_back(tri);
    }

    return true;
}

} // namespace

bool importMeshGltf(const QString& gltfFilePath, Mesh* outMesh, QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (outMesh == nullptr) {
        return fail(QStringLiteral("Output mesh pointer is null."));
    }

    GltfLoaded gltf{};
    const QFileInfo fileInfo(gltfFilePath);
    const bool isGlb = fileInfo.suffix().compare(QStringLiteral("glb"), Qt::CaseInsensitive) == 0;
    if (isGlb) {
        if (!readGlbFile(gltfFilePath, &gltf, errorMessage)) {
            return false;
        }
    } else {
        if (!readSeparateGltfFile(gltfFilePath, &gltf, errorMessage)) {
            return false;
        }
    }

    outMesh->triangles.clear();
    outMesh->materials.clear();
    outMesh->textures.clear();

    const QJsonArray materialsJson = gltf.root.value(QStringLiteral("materials")).toArray();
    for (const QJsonValue& value : materialsJson) {
        outMesh->materials.push_back(materialFromGltfJson(value.toObject()));
    }

    if (outMesh->materials.empty()) {
        outMesh->materials.push_back(MaterialGpu{});
    }

    const QJsonArray meshes = gltf.root.value(QStringLiteral("meshes")).toArray();
    if (meshes.isEmpty()) {
        return fail(QStringLiteral("glTF contains no meshes."));
    }

    for (const QJsonValue& meshValue : meshes) {
        const QJsonArray primitives = meshValue.toObject().value(QStringLiteral("primitives")).toArray();
        for (const QJsonValue& primValue : primitives) {
            const QJsonObject primitive = primValue.toObject();
            const int materialIndex = primitive.value(QStringLiteral("material")).toInt(0);
            if (materialIndex < 0 || static_cast<size_t>(materialIndex) >= outMesh->materials.size()) {
                return fail(QStringLiteral("Primitive references invalid material index."));
            }
            if (!importPrimitive(gltf, primitive, static_cast<uint32_t>(materialIndex), &outMesh->triangles, errorMessage)) {
                return false;
            }
        }
    }

    if (outMesh->triangles.empty()) {
        return fail(QStringLiteral("glTF import produced no triangles."));
    }

    return true;
}
