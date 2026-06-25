#include "Geometry/MathCore.h"
#include "MeshAccel/Mesh.h"
#include "MeshAccel/MeshAccelScene.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Sampling/LightSamplingCore.h"
#include "Sampling/MisCore.h"
#include "Texture/ProceduralTexture.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

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

MaterialGpu makeEmissiveMaterial(float emission, float areaScale = 1.0f)
{
    MaterialGpu material{};
    material.r = 0.9f * areaScale;
    material.g = 0.8f * areaScale;
    material.b = 0.2f * areaScale;
    material.emission = emission;
    material.materialType = static_cast<uint32_t>(MaterialType::Emissive);
    return material;
}

MeshTriangle makeXYTriangle(
    Vec3 v0,
    Vec3 v1,
    Vec3 v2,
    uint32_t materialIndex,
    Vec3 normal = vecMake3(0.0f, 0.0f, 1.0f))
{
    MeshTriangle tri{};
    tri.v0 = v0;
    tri.v1 = v1;
    tri.v2 = v2;
    tri.n0 = normal;
    tri.n1 = normal;
    tri.n2 = normal;
    tri.materialIndex = materialIndex;
    return tri;
}

Mesh buildTwoEmissiveLightsMesh()
{
    Mesh mesh{};
    mesh.materials.push_back(makeEmissiveMaterial(10.0f, 1.0f));
    mesh.materials.push_back(makeEmissiveMaterial(20.0f, 1.0f));

    mesh.triangles.push_back(makeXYTriangle(
        vecMake3(0.0f, 0.0f, 0.0f),
        vecMake3(1.0f, 0.0f, 0.0f),
        vecMake3(0.0f, 1.0f, 0.0f),
        0));

    mesh.triangles.push_back(makeXYTriangle(
        vecMake3(10.0f, 0.0f, 0.0f),
        vecMake3(12.0f, 0.0f, 0.0f),
        vecMake3(10.0f, 2.0f, 0.0f),
        1));

    return mesh;
}

Mesh buildSingleEmissiveLightMesh()
{
    Mesh mesh{};
    mesh.materials.push_back(makeEmissiveMaterial(5.0f));
    mesh.triangles.push_back(makeXYTriangle(
        vecMake3(0.0f, 0.0f, 0.0f),
        vecMake3(2.0f, 0.0f, 0.0f),
        vecMake3(0.0f, 2.0f, 0.0f),
        0));
    return mesh;
}

void testEmissiveTriangleCdfWeights()
{
    Mesh mesh = buildTwoEmissiveLightsMesh();
    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "two-light mesh builds");

    const std::vector<uint32_t>& indices = scene.emissiveTriangleIndicesHost();
    const std::vector<float>& cdf = scene.emissiveTriangleCdfHost();
    expectTrue(indices.size() == 2, "two emissive triangles collected");
    expectTrue(cdf.size() == 3, "prefix cdf has count + 1 entries");

    const float smallTriArea = 0.5f;
    const float largeTriArea = 2.0f;
    const float smallLuminance = estimateMaterialEmissionLuminance(mesh.materials[0], nullptr, 0);
    const float largeLuminance = estimateMaterialEmissionLuminance(mesh.materials[1], nullptr, 0);
    const float expectedSmallWeight = smallTriArea * smallLuminance;
    const float expectedLargeWeight = largeTriArea * largeLuminance;
    const float expectedTotal = expectedSmallWeight + expectedLargeWeight;

    expectNear(cdf[1], expectedSmallWeight, 1.0e-4f, "first triangle cdf weight");
    expectNear(cdf[2], expectedTotal, 1.0e-4f, "total cdf weight");

    const float smallSelectionPdf = lightEmissiveSelectionPdf(scene.hostScene(), 0);
    const float largeSelectionPdf = lightEmissiveSelectionPdf(scene.hostScene(), 1);
    expectNear(smallSelectionPdf, expectedSmallWeight / expectedTotal, 1.0e-5f, "small light selection pdf");
    expectNear(largeSelectionPdf, expectedLargeWeight / expectedTotal, 1.0e-5f, "large light selection pdf");
}

void testEmissivePdfConsistency()
{
    Mesh mesh = buildSingleEmissiveLightMesh();
    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "single-light mesh builds");

    const MeshAccelSceneGpu* gpuScene = scene.hostScene();
    expectTrue(gpuScene != nullptr, "host gpu scene available");

    const float uTri = 0.25f;
    const float u1 = 0.5f;
    const float u2 = 0.5f;

    Vec3 lightPosition{};
    Vec3 lightNormal{};
    Vec3 lightRadiance{};
    float areaPdf = 0.0f;
    expectTrue(
        lightSampleEmissiveTriangle(gpuScene, uTri, u1, u2, lightPosition, lightNormal, lightRadiance, areaPdf),
        "emissive triangle sample succeeds");

    const Vec3 shadingPoint = vecMake3(0.5f, 0.5f, 5.0f);
    const Vec3 toLight = vecSub3(lightPosition, shadingPoint);
    const Vec3 wi = vecNormalize3(toLight);
    const float cosLight = vecMax2(0.0f, vecDot3(lightNormal, vecScale3(wi, -1.0f)));
    expectTrue(cosLight > 0.0f, "sampled light faces shading point");

    const float dist2 = vecDot3(toLight, toLight);
    const float sampledSolidAnglePdf = areaPdf * dist2 / cosLight;

    uint32_t triangleIndex = gpuScene->emissiveTriangleIndices[0];
    Vec3 queryNormal{};
    const float evaluatedSolidAnglePdf = lightPdfEmissiveTriangleForIndex(
        gpuScene,
        shadingPoint,
        lightPosition,
        triangleIndex,
        queryNormal);

    expectNear(sampledSolidAnglePdf, evaluatedSolidAnglePdf, 1.0e-4f, "sampled and evaluated solid-angle pdf match");
}

void testDirectEmissionMisWeight()
{
    Mesh mesh = buildSingleEmissiveLightMesh();
    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "single-light mesh builds for MIS");

    const MeshAccelSceneGpu* gpuScene = scene.hostScene();
    const uint32_t triangleIndex = gpuScene->emissiveTriangleIndices[0];
    const Vec3 prevPosition = vecMake3(0.5f, 0.5f, 5.0f);
    const Vec3 hitPosition = vecMake3(0.5f, 0.5f, 0.0f);
    const Vec3 emission = lightEmissiveRadiance(mesh.materials[0]);
    const float prevBsdfPdf = 0.2f;

    Vec3 lightNormal{};
    const float lightPdf = lightPdfEmissiveTriangleForIndex(
        gpuScene, prevPosition, hitPosition, triangleIndex, lightNormal);
    expectTrue(lightPdf > 0.0f, "light pdf positive for MIS test");

    const float expectedWeight = misBalanceWeight(prevBsdfPdf, lightPdf);
    expectTrue(expectedWeight > 0.0f && expectedWeight < 1.0f, "MIS weight between 0 and 1");

    const Vec3 throughput = vecMake3(1.0f, 1.0f, 1.0f);
    const Vec3 weighted = lightDirectEmissionWithMis(
        gpuScene,
        prevPosition,
        hitPosition,
        triangleIndex,
        throughput,
        emission,
        prevBsdfPdf,
        true);

    expectNear(weighted.x, emission.x * expectedWeight, 1.0e-5f, "direct emission MIS scales radiance");
}

void testSurfaceNeeUsesNormalOffsetShadowRay()
{
    Mesh mesh = buildSingleEmissiveLightMesh();
    MeshAccelScene scene;
    expectTrue(scene.build(mesh), "single-light mesh builds for shadow test");

    const MeshAccelSceneGpu* gpuScene = scene.hostScene();
    const Vec3 position = vecMake3(0.5f, 0.5f, 5.0f);
    const Vec3 normal = vecMake3(0.0f, 0.0f, -1.0f);
    const Vec3 wi = vecNormalize3(vecMake3(0.0f, 0.0f, -1.0f));
    const float lightDistance = 5.0f;

    expectTrue(
        !lightIsOccludedBefore(position, normal, wi, lightDistance, gpuScene, 0.0f, UINT32_MAX),
        "surface NEE shadow ray reaches emissive triangle");
    expectTrue(
        lightTriangleIsEmissive(gpuScene, gpuScene->emissiveTriangleIndices[0]),
        "emissive triangle lookup succeeds");
}

} // namespace

int main()
{
    testEmissiveTriangleCdfWeights();
    testEmissivePdfConsistency();
    testDirectEmissionMisWeight();
    testSurfaceNeeUsesNormalOffsetShadowRay();

    if (gFailures == 0) {
        std::cout << "All PathTracer light sampling tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << gFailures << " test failure(s).\n";
    return EXIT_FAILURE;
}
