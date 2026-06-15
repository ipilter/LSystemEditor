#include "MeshAccelScene.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <cstdint>
#include <cmath>

namespace {

constexpr int kFloatPrecision = 6;

const char* materialKindLabel(MaterialKindGpu kind)
{
    switch (kind) {
    case 1:
        return "Metal";
    case 2:
        return "Glass";
    case 0:
    default:
        return "Diffuse";
    }
}

float specularExponentFromRoughness(float roughness)
{
    const float clamped = roughness < 0.0f ? 0.0f : (roughness > 1.0f ? 1.0f : roughness);
    return 10.0f + (1.0f - clamped) * 990.0f;
}

void writeMaterialEntry(QTextStream& out, uint32_t materialIndex, const MaterialGpu& material)
{
    const float ns = specularExponentFromRoughness(material.roughness);
    const float emissionScale = material.emission > 0.0f ? material.emission : 0.0f;

    out << "newmtl mat_" << materialIndex << '\n';
    out << "# PathTracer kind: " << materialKindLabel(material.kind) << '\n';
    out << "# PathTracer roughness: " << material.roughness << '\n';
    out << "# PathTracer metallic: " << material.metallic << '\n';
    out << "# PathTracer transmission: " << material.transmission << '\n';

    switch (material.kind) {
    case 1: {
        const float kdScale = 0.04f * (1.0f - material.metallic);
        out << "Ka " << material.r * 0.2f << ' ' << material.g * 0.2f << ' ' << material.b * 0.2f << '\n';
        out << "Kd " << material.r * kdScale << ' ' << material.g * kdScale << ' ' << material.b * kdScale << '\n';
        out << "Ks " << material.r << ' ' << material.g << ' ' << material.b << '\n';
        out << "Ns " << ns << '\n';
        out << "Ni " << material.ior << '\n';
        out << "d 1.0\n";
        break;
    }
    case 2: {
        const float alpha = material.transmission < 0.0f ? 0.0f
            : (material.transmission > 1.0f ? 1.0f : material.transmission);
        const float opacity = 1.0f - alpha;
        out << "Ka " << material.r * 0.2f << ' ' << material.g * 0.2f << ' ' << material.b * 0.2f << '\n';
        out << "Kd " << material.r << ' ' << material.g << ' ' << material.b << '\n';
        out << "Ks 0.04 0.04 0.04\n";
        out << "Ns " << ns << '\n';
        out << "Ni " << material.ior << '\n';
        out << "d " << opacity << '\n';
        break;
    }
    case 0:
    default:
        out << "Ka " << material.r * 0.2f << ' ' << material.g * 0.2f << ' ' << material.b * 0.2f << '\n';
        out << "Kd " << material.r << ' ' << material.g << ' ' << material.b << '\n';
        out << "Ks 0.04 0.04 0.04\n";
        out << "Ns " << ns << '\n';
        out << "Ni " << material.ior << '\n';
        out << "d 1.0\n";
        break;
    }

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
        writeVec3(objOut, 'vn', tri.n0);
        writeVec3(objOut, 'vn', tri.n1);
        writeVec3(objOut, 'vn', tri.n2);

        const int i0 = vertexIndex + 1;
        const int i1 = vertexIndex + 2;
        const int i2 = vertexIndex + 3;
        objOut << "f " << i0 << "//" << i0 << ' ' << i1 << "//" << i1 << ' ' << i2 << "//" << i2 << '\n';
        vertexIndex += 3;
    }

    objFile.close();
    return true;
}
