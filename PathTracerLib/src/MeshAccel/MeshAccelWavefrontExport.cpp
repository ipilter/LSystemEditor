#include "MeshAccelScene.h"
#include "MeshAccelMaterialJson.h"
#include "Medium/MediumProperties.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <cstdint>
#include <cmath>

namespace {

constexpr int kFloatPrecision = 6;

float clamp01(float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

float specularExponentFromRoughness(float roughness)
{
    const float clamped = clamp01(roughness);
    return 10.0f + (1.0f - clamped) * 990.0f;
}

void writeMaterialEntry(QTextStream& out, uint32_t materialIndex, const MaterialGpu& material)
{
    const float ns = specularExponentFromRoughness(material.roughness);
    const float emissionScale = material.emission > 0.0f ? material.emission : 0.0f;
    const float opacity = materialIsClearMedium(material) ? 0.05f : 1.0f;

    out << "newmtl mat_" << materialIndex << '\n';
    out << "# PathTracer r g b: " << material.r << ' ' << material.g << ' ' << material.b << '\n';
    out << "# PathTracer roughness: " << material.roughness << '\n';
    out << "# PathTracer metallic: " << material.metallic << '\n';
    out << "# PathTracer sigmaS: " << material.sigmaSr << ' ' << material.sigmaSg << ' ' << material.sigmaSb << '\n';
    out << "# PathTracer sigmaA: " << material.sigmaAr << ' ' << material.sigmaAg << ' ' << material.sigmaAb << '\n';
    out << "# PathTracer mediumG: " << material.mediumG << '\n';
    out << "# PathTracer ior: " << material.ior << '\n';

    out << "Ka " << material.r * 0.2f << ' ' << material.g * 0.2f << ' ' << material.b * 0.2f << '\n';
    out << "Kd " << material.r << ' ' << material.g << ' ' << material.b << '\n';
    out << "Ks " << material.r * material.metallic << ' ' << material.g * material.metallic << ' '
        << material.b * material.metallic << '\n';
    out << "Ns " << ns << '\n';
    out << "Ni " << material.ior << '\n';
    out << "d " << opacity << '\n';

    if (emissionScale > 0.0f) {
        out << "Ke " << material.r * emissionScale << ' ' << material.g * emissionScale << ' '
            << material.b * emissionScale << '\n';
    }

    out << '\n';
}

void writeVec3(QTextStream& out, char prefix, const Vec3& v)
{
    out << prefix << ' ' << v.x << ' ' << v.y << ' ' << v.z << '\n';
}

void writeVec2(QTextStream& out, char prefix, const Vec2& v)
{
    out << prefix << ' ' << v.x << ' ' << v.y << '\n';
}

} // namespace

bool MeshAccelScene::exportWavefrontObj(const QString& objFilePath, QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (!m_built) {
        return fail(QStringLiteral("Scene has not been built."));
    }

    if (m_triangles.empty()) {
        return fail(QStringLiteral("Scene has no geometry to export."));
    }

    const QFileInfo objInfo(objFilePath);
    if (!objInfo.dir().exists()) {
        return fail(QStringLiteral("Output directory does not exist."));
    }

    const QString mtlFileName = objInfo.completeBaseName() + QStringLiteral(".mtl");
    const QString mtlFilePath = objInfo.dir().filePath(mtlFileName);

    QFile mtlFile(mtlFilePath);
    if (!mtlFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return fail(QStringLiteral("Failed to open MTL file for writing: %1").arg(mtlFilePath));
    }

    QTextStream mtlOut(&mtlFile);
    mtlOut.setRealNumberNotation(QTextStream::FixedNotation);
    mtlOut.setRealNumberPrecision(kFloatPrecision);
    mtlOut << "# PathTracer Wavefront MTL export\n\n";

    for (uint32_t materialIndex = 0; materialIndex < m_materials.size(); ++materialIndex) {
        writeMaterialEntry(mtlOut, materialIndex, m_materials[materialIndex]);
    }

    mtlFile.close();

    const QString materialsJsonPath =
        objInfo.dir().filePath(objInfo.completeBaseName() + QStringLiteral("_materials.json"));
    if (!writeMaterialsJsonSidecar(materialsJsonPath, m_materials, m_textures, errorMessage)) {
        return false;
    }

    QFile objFile(objFilePath);
    if (!objFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return fail(QStringLiteral("Failed to open OBJ file for writing: %1").arg(objFilePath));
    }

    QTextStream objOut(&objFile);
    objOut.setRealNumberNotation(QTextStream::FixedNotation);
    objOut.setRealNumberPrecision(kFloatPrecision);
    objOut << "# PathTracer Wavefront OBJ export\n";
    objOut << "mtllib " << mtlFileName << '\n';
    objOut << "o scene\n";

    uint32_t currentMaterialIndex = UINT32_MAX;
    int vertexIndex = 0;

    for (const TriangleGpu& tri : m_triangles) {
        if (tri.materialIndex != currentMaterialIndex) {
            currentMaterialIndex = tri.materialIndex;
            objOut << "usemtl mat_" << currentMaterialIndex << '\n';
        }

        writeVec3(objOut, 'v', tri.v0);
        writeVec3(objOut, 'v', tri.v1);
        writeVec3(objOut, 'v', tri.v2);
        writeVec2(objOut, 'vt', tri.uv0);
        writeVec2(objOut, 'vt', tri.uv1);
        writeVec2(objOut, 'vt', tri.uv2);
        writeVec3(objOut, 'vn', tri.n0);
        writeVec3(objOut, 'vn', tri.n1);
        writeVec3(objOut, 'vn', tri.n2);

        const int i0 = vertexIndex + 1;
        const int i1 = vertexIndex + 2;
        const int i2 = vertexIndex + 3;
        objOut << "f " << i0 << '/' << i0 << '/' << i0 << ' '
               << i1 << '/' << i1 << '/' << i1 << ' '
               << i2 << '/' << i2 << '/' << i2 << '\n';
        vertexIndex += 3;
    }

    objFile.close();
    return true;
}
