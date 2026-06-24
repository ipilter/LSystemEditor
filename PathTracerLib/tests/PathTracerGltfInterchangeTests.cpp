#include "MeshAccel/MeshAccelGltfIO.h"
#include "MeshAccel/MeshAccelMaterialJson.h"
#include "MeshAccel/MaterialType.h"
#include "MeshAccel/MeshAccelTypes.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

namespace {

int gFailures = 0;

void expectTrue(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++gFailures;
    }
}

void expectNear(float actual, float expected, float tolerance, const char* message)
{
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " (actual=" << actual << ", expected=" << expected << ")\n";
        ++gFailures;
    }
}

MeshTriangle makeTestTriangle(uint32_t materialIndex, float zOffset = 0.0f)
{
    MeshTriangle tri{};
    tri.v0 = Vec3{0.0f, 0.0f, zOffset};
    tri.v1 = Vec3{100.0f, 0.0f, zOffset};
    tri.v2 = Vec3{0.0f, 100.0f, zOffset};
    tri.n0 = tri.n1 = tri.n2 = Vec3{0.0f, 0.0f, 1.0f};
    tri.uv0 = Vec2{0.0f, 0.0f};
    tri.uv1 = Vec2{1.0f, 0.0f};
    tri.uv2 = Vec2{0.0f, 1.0f};
    tri.materialIndex = materialIndex;
    return tri;
}

MaterialGpu makeDiffuseMaterial()
{
    MaterialGpu material{};
    material.r = 0.2f;
    material.g = 0.4f;
    material.b = 0.6f;
    material.roughness = 0.75f;
    material.metallic = 0.0f;
    material.diffuseRoughness = 0.3f;
    material.specular = 0.8f;
    material.materialType = static_cast<uint32_t>(MaterialType::Opaque);
    return material;
}

MaterialGpu makeMetalMaterial()
{
    MaterialGpu material{};
    material.r = 0.9f;
    material.g = 0.85f;
    material.b = 0.8f;
    material.roughness = 0.15f;
    material.metallic = 1.0f;
    material.materialType = static_cast<uint32_t>(MaterialType::Opaque);
    return material;
}

MaterialGpu makeGlassMaterial()
{
    MaterialGpu material{};
    material.r = 0.95f;
    material.g = 0.98f;
    material.b = 1.0f;
    material.roughness = 0.02f;
    material.metallic = 0.0f;
    material.ior = 1.52f;
    material.materialType = static_cast<uint32_t>(MaterialType::Glass);
    return material;
}

MaterialGpu makeSubsurfaceMaterial()
{
    MaterialGpu material{};
    material.r = 0.9f;
    material.g = 0.95f;
    material.b = 0.2f;
    material.roughness = 0.8f;
    material.ior = 1.5f;
    material.materialType = static_cast<uint32_t>(MaterialType::Subsurface);
    material.subsurface = 1.0f;
    material.subsurfaceRadiusR = 2.0f;
    material.subsurfaceRadiusG = 2.0f;
    material.subsurfaceRadiusB = 2.0f;
    return material;
}

MaterialGpu makeEmissionMaterial()
{
    MaterialGpu material{};
    material.r = 1.0f;
    material.g = 0.8f;
    material.b = 0.6f;
    material.emission = 12.0f;
    material.materialType = static_cast<uint32_t>(MaterialType::Emissive);
    return material;
}

std::vector<TriangleGpu> toTriangleGpu(const std::vector<MeshTriangle>& tris)
{
    std::vector<TriangleGpu> gpu;
    gpu.reserve(tris.size());
    for (const MeshTriangle& tri : tris) {
        TriangleGpu g{};
        g.v0 = tri.v0;
        g.v1 = tri.v1;
        g.v2 = tri.v2;
        g.n0 = tri.n0;
        g.n1 = tri.n1;
        g.n2 = tri.n2;
        g.uv0 = tri.uv0;
        g.uv1 = tri.uv1;
        g.uv2 = tri.uv2;
        g.materialIndex = tri.materialIndex;
        gpu.push_back(g);
    }
    return gpu;
}

void testMaterialExtrasRoundTrip()
{
    const MaterialGpu source = makeDiffuseMaterial();
    const QJsonObject extras = QJsonObject{
        {QStringLiteral("pathTracer"), pathTracerMaterialExtrasJson(source)}};

    MaterialGpu restored{};
    applyPathTracerMaterialExtrasJson(extras, &restored);

    expectNear(restored.diffuseRoughness, source.diffuseRoughness, 1e-5f, "extras diffuseRoughness");
    expectNear(restored.specular, source.specular, 1e-5f, "extras specular");
    expectNear(restored.materialType, source.materialType, 1e-5f, "extras materialType");
    expectNear(restored.subsurface, source.subsurface, 1e-5f, "extras subsurface");
    expectNear(restored.subsurfaceRadiusR, source.subsurfaceRadiusR, 1e-5f, "extras subsurfaceRadius.r");
    expectNear(restored.sigmaAr, source.sigmaAr, 1e-5f, "extras sigmaA.r");
    expectNear(restored.sigmaSr, source.sigmaSr, 1e-5f, "extras sigmaS.r");
    expectNear(restored.mediumG, source.mediumG, 1e-5f, "extras mediumG");
    expectNear(restored.abbeNumber, source.abbeNumber, 1e-5f, "extras abbeNumber");
}

void testGltfRoundTripMaterial(const MaterialGpu& source, const char* label)
{
    QTemporaryDir tempDir;
    expectTrue(tempDir.isValid(), "temporary directory for glTF round-trip");

    const QString glbPath = tempDir.filePath(QStringLiteral("roundtrip.glb"));

    std::vector<MeshTriangle> tris{makeTestTriangle(0)};
    const std::vector<TriangleGpu> gpuTris = toTriangleGpu(tris);
    const std::vector<MaterialGpu> materials{source};

    QString exportError;
    expectTrue(exportMeshGltf(gpuTris, materials, {}, glbPath, &exportError), label);

    Mesh imported{};
    QString importError;
    expectTrue(importMeshGltf(glbPath, &imported, &importError), label);

    expectTrue(!imported.triangles.empty(), "imported mesh has triangles");
    expectTrue(imported.materials.size() == 1, "imported mesh has one material");

    const MaterialGpu& restored = imported.materials.front();
    expectNear(restored.r, source.r, 1e-3f, "round-trip albedo.r");
    expectNear(restored.g, source.g, 1e-3f, "round-trip albedo.g");
    expectNear(restored.b, source.b, 1e-3f, "round-trip albedo.b");
    expectNear(restored.roughness, source.roughness, 1e-3f, "round-trip roughness");
    expectNear(restored.metallic, source.metallic, 1e-3f, "round-trip metallic");
    expectNear(restored.ior, source.ior, 1e-3f, "round-trip ior");
    expectNear(restored.emission, source.emission, 1e-3f, "round-trip emission");
    expectNear(restored.diffuseRoughness, source.diffuseRoughness, 1e-3f, "round-trip diffuseRoughness");
    expectNear(restored.specular, source.specular, 1e-3f, "round-trip specular");
    expectNear(static_cast<float>(restored.materialType), static_cast<float>(source.materialType), 1e-3f, "round-trip materialType");
    expectNear(restored.subsurface, source.subsurface, 1e-3f, "round-trip subsurface");
    expectNear(restored.subsurfaceRadiusR, source.subsurfaceRadiusR, 1e-3f, "round-trip subsurfaceRadiusR");
    expectNear(restored.abbeNumber, source.abbeNumber, 1e-3f, "round-trip abbeNumber");

    const MeshTriangle& tri = imported.triangles.front();
    expectNear(tri.v0.x, 0.0f, 1e-2f, "round-trip vertex scale x0");
    expectNear(tri.v1.x, 100.0f, 1e-2f, "round-trip vertex scale x1");
}

void testMultiMaterialExport()
{
    QTemporaryDir tempDir;
    expectTrue(tempDir.isValid(), "temporary directory for multi-material export");

    const QString glbPath = tempDir.filePath(QStringLiteral("multi.glb"));

    std::vector<MeshTriangle> tris{
        makeTestTriangle(0, 0.0f),
        makeTestTriangle(1, 10.0f),
    };
    const std::vector<MaterialGpu> materials{makeDiffuseMaterial(), makeMetalMaterial()};

    QString error;
    expectTrue(exportMeshGltf(toTriangleGpu(tris), materials, {}, glbPath, &error), "multi-material export");

    Mesh imported{};
    expectTrue(importMeshGltf(glbPath, &imported, &error), "multi-material import");
    expectTrue(imported.materials.size() == 2, "two materials imported");
    expectTrue(imported.triangles.size() == 2, "two triangles imported");
}

} // namespace

int main()
{
    testMaterialExtrasRoundTrip();
    testGltfRoundTripMaterial(makeDiffuseMaterial(), "diffuse glTF round-trip");
    testGltfRoundTripMaterial(makeMetalMaterial(), "metal glTF round-trip");
    testGltfRoundTripMaterial(makeGlassMaterial(), "glass glTF round-trip");
    testGltfRoundTripMaterial(makeSubsurfaceMaterial(), "subsurface glTF round-trip");
    testGltfRoundTripMaterial(makeEmissionMaterial(), "emission glTF round-trip");
    testMultiMaterialExport();

    if (gFailures != 0) {
        std::cerr << gFailures << " test failure(s)\n";
        return 1;
    }

    std::cout << "All glTF interchange tests passed.\n";
    return 0;
}
