#include "MeshAccelMaterialJson.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QJsonArray floatArray3(float x, float y, float z)
{
    QJsonArray arr;
    arr.append(static_cast<double>(x));
    arr.append(static_cast<double>(y));
    arr.append(static_cast<double>(z));
    return arr;
}

QJsonArray floatArray4(float x, float y, float z, float w)
{
    QJsonArray arr;
    arr.append(static_cast<double>(x));
    arr.append(static_cast<double>(y));
    arr.append(static_cast<double>(z));
    arr.append(static_cast<double>(w));
    return arr;
}

QJsonObject textureDescToJson(const TextureDescGpu& texture)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("kind"), static_cast<int>(texture.kind));
    obj.insert(QStringLiteral("p0"), floatArray4(texture.p0.x, texture.p0.y, texture.p0.z, texture.p0.w));
    obj.insert(QStringLiteral("p1"), floatArray4(texture.p1.x, texture.p1.y, texture.p1.z, texture.p1.w));
    obj.insert(QStringLiteral("p2"), floatArray4(texture.p2.x, texture.p2.y, texture.p2.z, texture.p2.w));
    return obj;
}

bool textureDescFromJson(const QJsonObject& obj, TextureDescGpu* outTexture)
{
    if (outTexture == nullptr) {
        return false;
    }
    const QJsonValue kindValue = obj.value(QStringLiteral("kind"));
    if (!kindValue.isDouble()) {
        return false;
    }
    outTexture->kind = static_cast<uint32_t>(kindValue.toInt());

    auto readFloat4 = [](const QJsonObject& source, const char* key, float4& out) {
        const QJsonArray arr = source.value(QString::fromLatin1(key)).toArray();
        if (arr.size() < 4) {
            return false;
        }
        out.x = static_cast<float>(arr.at(0).toDouble());
        out.y = static_cast<float>(arr.at(1).toDouble());
        out.z = static_cast<float>(arr.at(2).toDouble());
        out.w = static_cast<float>(arr.at(3).toDouble());
        return true;
    };

    return readFloat4(obj, "p0", outTexture->p0)
        && readFloat4(obj, "p1", outTexture->p1)
        && readFloat4(obj, "p2", outTexture->p2);
}

QJsonObject materialGpuToJson(const MaterialGpu& material)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("albedo"), floatArray3(material.r, material.g, material.b));
    obj.insert(QStringLiteral("roughness"), static_cast<double>(material.roughness));
    obj.insert(QStringLiteral("metallic"), static_cast<double>(material.metallic));
    obj.insert(QStringLiteral("emission"), static_cast<double>(material.emission));
    obj.insert(QStringLiteral("diffuseRoughness"), static_cast<double>(material.diffuseRoughness));
    obj.insert(QStringLiteral("specular"), static_cast<double>(material.specular));
    obj.insert(QStringLiteral("sigmaA"), floatArray3(material.sigmaAr, material.sigmaAg, material.sigmaAb));
    obj.insert(QStringLiteral("sigmaS"), floatArray3(material.sigmaSr, material.sigmaSg, material.sigmaSb));
    obj.insert(QStringLiteral("mediumG"), static_cast<double>(material.mediumG));
    obj.insert(QStringLiteral("ior"), static_cast<double>(material.ior));
    obj.insert(QStringLiteral("abbeNumber"), static_cast<double>(material.abbeNumber));
    obj.insert(QStringLiteral("materialType"), static_cast<int>(material.materialType));
    obj.insert(QStringLiteral("subsurface"), static_cast<double>(material.subsurface));
    obj.insert(QStringLiteral("subsurfaceRadius"), floatArray3(
        material.subsurfaceRadiusR, material.subsurfaceRadiusG, material.subsurfaceRadiusB));
    obj.insert(QStringLiteral("roughnessTex"), static_cast<int>(material.roughnessTex));
    obj.insert(QStringLiteral("metallicTex"), static_cast<int>(material.metallicTex));
    obj.insert(QStringLiteral("emissionTex"), static_cast<int>(material.emissionTex));
    obj.insert(QStringLiteral("diffuseRoughnessTex"), static_cast<int>(material.diffuseRoughnessTex));
    obj.insert(QStringLiteral("specularTex"), static_cast<int>(material.specularTex));
    obj.insert(QStringLiteral("sigmaATex"), static_cast<int>(material.sigmaATex));
    obj.insert(QStringLiteral("sigmaSTex"), static_cast<int>(material.sigmaSTex));
    obj.insert(QStringLiteral("mediumGTex"), static_cast<int>(material.mediumGTex));
    obj.insert(QStringLiteral("iorTex"), static_cast<int>(material.iorTex));
    return obj;
}

bool materialGpuFromJson(const QJsonObject& obj, MaterialGpu* outMaterial)
{
    if (outMaterial == nullptr) {
        return false;
    }
    MaterialGpu material{};

    const QJsonArray albedo = obj.value(QStringLiteral("albedo")).toArray();
    if (albedo.size() >= 3) {
        material.r = static_cast<float>(albedo.at(0).toDouble());
        material.g = static_cast<float>(albedo.at(1).toDouble());
        material.b = static_cast<float>(albedo.at(2).toDouble());
    }

    material.roughness = static_cast<float>(obj.value(QStringLiteral("roughness")).toDouble(0.5));
    material.metallic = static_cast<float>(obj.value(QStringLiteral("metallic")).toDouble(0.0));
    material.emission = static_cast<float>(obj.value(QStringLiteral("emission")).toDouble(0.0));
    material.diffuseRoughness = static_cast<float>(obj.value(QStringLiteral("diffuseRoughness")).toDouble(-1.0));
    material.specular = static_cast<float>(obj.value(QStringLiteral("specular")).toDouble(1.0));

    const QJsonArray sigmaA = obj.value(QStringLiteral("sigmaA")).toArray();
    if (sigmaA.size() >= 3) {
        material.sigmaAr = static_cast<float>(sigmaA.at(0).toDouble());
        material.sigmaAg = static_cast<float>(sigmaA.at(1).toDouble());
        material.sigmaAb = static_cast<float>(sigmaA.at(2).toDouble());
    }

    const QJsonArray sigmaS = obj.value(QStringLiteral("sigmaS")).toArray();
    if (sigmaS.size() >= 3) {
        material.sigmaSr = static_cast<float>(sigmaS.at(0).toDouble());
        material.sigmaSg = static_cast<float>(sigmaS.at(1).toDouble());
        material.sigmaSb = static_cast<float>(sigmaS.at(2).toDouble());
    }

    material.mediumG = static_cast<float>(obj.value(QStringLiteral("mediumG")).toDouble(0.0));
    material.ior = static_cast<float>(obj.value(QStringLiteral("ior")).toDouble(1.5));
    material.abbeNumber = static_cast<float>(obj.value(QStringLiteral("abbeNumber")).toDouble(58.0));
    material.materialType = static_cast<uint32_t>(obj.value(QStringLiteral("materialType")).toInt(0));
    material.subsurface = static_cast<float>(obj.value(QStringLiteral("subsurface")).toDouble(0.0));
    const QJsonArray subsurfaceRadius = obj.value(QStringLiteral("subsurfaceRadius")).toArray();
    if (subsurfaceRadius.size() >= 3) {
        material.subsurfaceRadiusR = static_cast<float>(subsurfaceRadius.at(0).toDouble(1.0));
        material.subsurfaceRadiusG = static_cast<float>(subsurfaceRadius.at(1).toDouble(1.0));
        material.subsurfaceRadiusB = static_cast<float>(subsurfaceRadius.at(2).toDouble(1.0));
    }

    material.albedoTex = static_cast<uint32_t>(obj.value(QStringLiteral("albedoTex")).toInt(0));
    material.roughnessTex = static_cast<uint32_t>(obj.value(QStringLiteral("roughnessTex")).toInt(0));
    material.metallicTex = static_cast<uint32_t>(obj.value(QStringLiteral("metallicTex")).toInt(0));
    material.emissionTex = static_cast<uint32_t>(obj.value(QStringLiteral("emissionTex")).toInt(0));
    material.diffuseRoughnessTex = static_cast<uint32_t>(obj.value(QStringLiteral("diffuseRoughnessTex")).toInt(0));
    material.specularTex = static_cast<uint32_t>(obj.value(QStringLiteral("specularTex")).toInt(0));
    material.sigmaATex = static_cast<uint32_t>(obj.value(QStringLiteral("sigmaATex")).toInt(0));
    material.sigmaSTex = static_cast<uint32_t>(obj.value(QStringLiteral("sigmaSTex")).toInt(0));
    material.mediumGTex = static_cast<uint32_t>(obj.value(QStringLiteral("mediumGTex")).toInt(0));
    material.iorTex = static_cast<uint32_t>(obj.value(QStringLiteral("iorTex")).toInt(0));

    *outMaterial = material;
    return true;
}

} // namespace

QJsonObject pathTracerMaterialExtrasJson(const MaterialGpu& material)
{
    QJsonObject volume;
    volume.insert(QStringLiteral("sigmaA"), floatArray3(material.sigmaAr, material.sigmaAg, material.sigmaAb));
    volume.insert(QStringLiteral("sigmaS"), floatArray3(material.sigmaSr, material.sigmaSg, material.sigmaSb));
    volume.insert(QStringLiteral("mediumG"), static_cast<double>(material.mediumG));
    volume.insert(QStringLiteral("abbeNumber"), static_cast<double>(material.abbeNumber));

    QJsonObject pathTracer;
    pathTracer.insert(QStringLiteral("diffuseRoughness"), static_cast<double>(material.diffuseRoughness));
    pathTracer.insert(QStringLiteral("specular"), static_cast<double>(material.specular));
    pathTracer.insert(QStringLiteral("materialType"), static_cast<int>(material.materialType));
    pathTracer.insert(QStringLiteral("subsurface"), static_cast<double>(material.subsurface));
    pathTracer.insert(QStringLiteral("subsurfaceRadius"), floatArray3(
        material.subsurfaceRadiusR, material.subsurfaceRadiusG, material.subsurfaceRadiusB));
    pathTracer.insert(QStringLiteral("volume"), volume);
    return pathTracer;
}

void applyPathTracerMaterialExtrasJson(const QJsonObject& extras, MaterialGpu* material)
{
    if (material == nullptr) {
        return;
    }

    const QJsonObject pathTracer = extras.value(QStringLiteral("pathTracer")).toObject();
    if (pathTracer.isEmpty()) {
        return;
    }

    if (pathTracer.contains(QStringLiteral("diffuseRoughness"))) {
        material->diffuseRoughness = static_cast<float>(pathTracer.value(QStringLiteral("diffuseRoughness")).toDouble());
    }
    if (pathTracer.contains(QStringLiteral("specular"))) {
        material->specular = static_cast<float>(pathTracer.value(QStringLiteral("specular")).toDouble());
    }
    if (pathTracer.contains(QStringLiteral("materialType"))) {
        material->materialType = static_cast<uint32_t>(pathTracer.value(QStringLiteral("materialType")).toInt());
    }
    if (pathTracer.contains(QStringLiteral("subsurface"))) {
        material->subsurface = static_cast<float>(pathTracer.value(QStringLiteral("subsurface")).toDouble());
    }
    const QJsonArray subsurfaceRadius = pathTracer.value(QStringLiteral("subsurfaceRadius")).toArray();
    if (subsurfaceRadius.size() >= 3) {
        material->subsurfaceRadiusR = static_cast<float>(subsurfaceRadius.at(0).toDouble());
        material->subsurfaceRadiusG = static_cast<float>(subsurfaceRadius.at(1).toDouble());
        material->subsurfaceRadiusB = static_cast<float>(subsurfaceRadius.at(2).toDouble());
    }

    const QJsonObject volume = pathTracer.value(QStringLiteral("volume")).toObject();
    if (!volume.isEmpty()) {
        const QJsonArray sigmaA = volume.value(QStringLiteral("sigmaA")).toArray();
        if (sigmaA.size() >= 3) {
            material->sigmaAr = static_cast<float>(sigmaA.at(0).toDouble());
            material->sigmaAg = static_cast<float>(sigmaA.at(1).toDouble());
            material->sigmaAb = static_cast<float>(sigmaA.at(2).toDouble());
        }
        const QJsonArray sigmaS = volume.value(QStringLiteral("sigmaS")).toArray();
        if (sigmaS.size() >= 3) {
            material->sigmaSr = static_cast<float>(sigmaS.at(0).toDouble());
            material->sigmaSg = static_cast<float>(sigmaS.at(1).toDouble());
            material->sigmaSb = static_cast<float>(sigmaS.at(2).toDouble());
        }
        if (volume.contains(QStringLiteral("mediumG"))) {
            material->mediumG = static_cast<float>(volume.value(QStringLiteral("mediumG")).toDouble());
        }
        if (volume.contains(QStringLiteral("abbeNumber"))) {
            material->abbeNumber = static_cast<float>(volume.value(QStringLiteral("abbeNumber")).toDouble());
        }
    }
}

bool writeMaterialsJsonSidecar(
    const QString& jsonFilePath,
    const std::vector<MaterialGpu>& materials,
    const std::vector<TextureDescGpu>& textures,
    QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("PathTracerMaterials/1"));
    root.insert(QStringLiteral("units"), QStringLiteral("millimeters"));

    QJsonArray materialArray;
    for (const MaterialGpu& material : materials) {
        materialArray.append(materialGpuToJson(material));
    }
    root.insert(QStringLiteral("materials"), materialArray);

    QJsonArray textureArray;
    for (const TextureDescGpu& texture : textures) {
        textureArray.append(textureDescToJson(texture));
    }
    root.insert(QStringLiteral("textures"), textureArray);

    QFile file(jsonFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return fail(QStringLiteral("Failed to open materials JSON for writing: %1").arg(jsonFilePath));
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool readMaterialsJsonSidecar(
    const QString& jsonFilePath,
    std::vector<MaterialGpu>* outMaterials,
    std::vector<TextureDescGpu>* outTextures,
    QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (outMaterials == nullptr || outTextures == nullptr) {
        return fail(QStringLiteral("Output pointers are null."));
    }

    QFile file(jsonFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fail(QStringLiteral("Failed to open materials JSON: %1").arg(jsonFilePath));
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(QStringLiteral("Invalid materials JSON: %1").arg(parseError.errorString()));
    }

    const QJsonObject root = doc.object();
    outMaterials->clear();
    outTextures->clear();

    const QJsonArray materialArray = root.value(QStringLiteral("materials")).toArray();
    outMaterials->reserve(materialArray.size());
    for (const QJsonValue& value : materialArray) {
        MaterialGpu material{};
        if (!materialGpuFromJson(value.toObject(), &material)) {
            return fail(QStringLiteral("Invalid material entry in JSON sidecar."));
        }
        outMaterials->push_back(material);
    }

    const QJsonArray textureArray = root.value(QStringLiteral("textures")).toArray();
    outTextures->reserve(textureArray.size());
    for (const QJsonValue& value : textureArray) {
        TextureDescGpu texture{};
        if (!textureDescFromJson(value.toObject(), &texture)) {
            return fail(QStringLiteral("Invalid texture entry in JSON sidecar."));
        }
        outTextures->push_back(texture);
    }

    return true;
}
