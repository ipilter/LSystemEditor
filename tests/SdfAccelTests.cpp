#include "SdfAccelTestHelpers.h"
#include "Sdf/Shapes/CappedConeSdf.h"
#include "Sdf/Shapes/CylinderSdf.h"
#include "Sdf/Shapes/SphereSdf.h"
#include "SdfAccelBoundsCore.h"
#include "SdfAccelBoundsMesh.h"
#include "SdfSceneContent.h"

#include <QColor>

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>

using namespace SdfAccelTest;

namespace {

std::vector<std::unique_ptr<SdfShape>> makeSingleShape(std::unique_ptr<SdfShape> shape)
{
    std::vector<std::unique_ptr<SdfShape>> shapes;
    shapes.push_back(std::move(shape));
    return shapes;
}

void testGpuStructLayout()
{
    expectTrue(sizeof(SdfAccelObjectGpu) == 64, "SdfAccelObjectGpuSize");
    expectTrue(sizeof(SdfAccelPayloadGpu) == 48, "SdfAccelPayloadGpuSize");
    expectTrue(sizeof(SdfBvhNode) == 48, "SdfBvhNodeSize");
    expectTrue(sizeof(SdfAccelSceneGpu) == 64, "SdfAccelSceneGpuSize");

    expectTrue(offsetof(SdfAccelObjectGpu, center) == 16, "SdfAccelObjectCenterOffset");
    expectTrue(offsetof(SdfBvhNode, leftIndex) == 32, "SdfBvhNodeLeftIndexOffset");
}

void testBuildEmptyFails()
{
    SdfAccelScene scene;
    expectTrue(!scene.build(), "BuildEmptyFails");
}

void testSingleSphereEval()
{
    auto shapes = makeSingleShape(std::make_unique<SphereSdf>(sdfMakeFloat3(1.0f, 2.0f, 3.0f), 0.5f));
    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "SingleSphereBuild");

    const SphereSdf oracle(sdfMakeFloat3(1.0f, 2.0f, 3.0f), 0.5f);
    expectNear(
        accelSceneEvalSDF(scene, sdfMakeFloat3(1.5f, 2.0f, 3.0f)),
        oracle.evalWorld(sdfMakeFloat3(1.5f, 2.0f, 3.0f)),
        1.0e-5f,
        "SingleSphereSurface");
    expectNear(
        accelSceneEvalSDF(scene, sdfMakeFloat3(1.0f, 2.0f, 3.0f)),
        oracle.evalWorld(sdfMakeFloat3(1.0f, 2.0f, 3.0f)),
        1.0e-5f,
        "SingleSphereCenter");
    expectTrue(accelSceneEvalSDF(scene, sdfMakeFloat3(10.0f, 10.0f, 10.0f)) > 0.0f, "SingleSphereOutside");
}

void testDefaultLayoutParity()
{
    auto shapes = sdfDefaultSceneShapes();
    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "DefaultLayoutBuild");

    for (float x = -4.0f; x <= 4.0f; x += 0.5f) {
        for (float y = -2.0f; y <= 2.0f; y += 0.5f) {
            for (float z = -2.0f; z <= 2.0f; z += 0.5f) {
                const SdfFloat3 p = sdfMakeFloat3(x, y, z);
                expectNear(
                    accelSceneEvalSDF(scene, p),
                    bruteSceneSDF(p, shapes),
                    1.0e-4f,
                    "DefaultLayoutGridParity");
            }
        }
    }
}

void testRayMarchParity()
{
    auto shapes = sdfDefaultSceneShapes();
    SdfAccelScene accelScene;
    sdfAccelPopulateScene(accelScene, shapes);
    expectTrue(accelScene.build(), "RayMarchParityBuild");

    const SdfMarchParamsGpu params = defaultMarchParams();

    struct Case
    {
        SdfFloat3 ro;
        SdfFloat3 rd;
        bool expectHit;
        const char* name;
    };

    const Case cases[] = {
        {sdfMakeFloat3(-8.0f, 0.0f, 0.0f), sdfMakeFloat3(1.0f, 0.0f, 0.0f), true, "SphereSideHit"},
        {sdfMakeFloat3(0.0f, 5.0f, 0.0f), sdfMakeFloat3(0.0f, -1.0f, 0.0f), true, "SphereTopHit"},
        {sdfMakeFloat3(-8.0f, 0.0f, 0.6f), sdfMakeFloat3(1.0f, 0.0f, 0.0f), false, "SphereSideMiss"},
    };

    for (const Case& testCase : cases) {
        const SdfHit accelHit = accelSceneRayMarch(accelScene, testCase.ro, testCase.rd, params);
        const SdfHit bruteHit = bruteForceRayMarch(testCase.ro, testCase.rd, shapes, params);
        expectTrue(accelHit.hit == testCase.expectHit, testCase.name);
        expectTrue(bruteHit.hit == testCase.expectHit, testCase.name);
        if (testCase.expectHit) {
            expectNear(accelHit.t, bruteHit.t, 1.0e-2f, testCase.name);
        }
    }
}

void testConservativeLowerBound()
{
    auto shapes = sdfDefaultSceneShapes();
    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "ConservativeLowerBoundBuild");

    std::mt19937 rng(0xC0FFEE01u);
    std::uniform_real_distribution<float> dist(-4.0f, 4.0f);
    for (int i = 0; i < 500; ++i) {
        const SdfFloat3 p = sdfMakeFloat3(dist(rng), dist(rng), dist(rng));
        const float exact = accelSceneEvalSDF(scene, p);
        const float conservative = accelSceneEvalSDFConservative(scene, p);
        if (exact > 0.0f) {
            expectTrue(conservative <= exact + 1.0e-4f, "ConservativeLowerBoundDefault");
        }
    }

    auto thinShapes = makeSingleShape(
        std::make_unique<CylinderSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), sdfMakeFloat2(0.02f, 5.0f)));
    SdfAccelScene thinScene;
    sdfAccelPopulateScene(thinScene, thinShapes);
    expectTrue(thinScene.build(), "ConservativeLowerBoundThinBuild");

    std::mt19937 thinRng(0x71110001u);
    std::uniform_real_distribution<float> thinDist(-5.0f, 5.0f);
    for (int i = 0; i < 300; ++i) {
        const SdfFloat3 p = sdfMakeFloat3(thinDist(thinRng), thinDist(thinRng), thinDist(thinRng));
        const float exact = accelSceneEvalSDF(thinScene, p);
        const float conservative = accelSceneEvalSDFConservative(thinScene, p);
        if (exact > 0.0f) {
            expectTrue(conservative <= exact + 1.0e-4f, "ConservativeLowerBoundThin");
        }
    }
}

void testStraddleConservativeOpenSpace()
{
    auto shapes = makeSingleShape(std::make_unique<SphereSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), 0.5f));
    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "StraddleConservativeBuild");

    const SdfFloat3 openPoint = sdfMakeFloat3(0.4f, 0.4f, 0.4f);
    const float exact = accelSceneEvalSDF(scene, openPoint);
    const float conservative = accelSceneEvalSDFConservative(scene, openPoint);
    expectTrue(exact > 0.1f, "StraddleConservativeExactPositive");
    expectTrue(conservative <= exact + 1.0e-4f, "StraddleConservativeLowerBound");
    expectTrue(conservative > 1.0e-3f, "StraddleConservativeNotEpsilonCrawl");
}

void testAnalyticalObjectsFlagged()
{
    auto shapes = makeSingleShape(std::make_unique<SphereSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), 0.5f));
    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "AnalyticalObjectsBuild");
    expectTrue((scene.objectsHost()[0].flags & SdfObjectFlagAnalytical) != 0, "AnalyticalFlagSet");
}

void testBvhBuiltForScene()
{
    auto shapes = sdfDefaultSceneShapes();
    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "BvhBuiltBuild");

    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    expectTrue(hostScene != nullptr, "BvhBuiltHostScene");
    expectTrue(hostScene->bvhNodeCount > 0, "BvhBuiltNodeCount");
    expectTrue(hostScene->bvhRootIndex < hostScene->bvhNodeCount, "BvhBuiltValidRoot");
    expectTrue(!scene.bvhNodesHost().empty(), "BvhBuiltHostVector");
}

void testAccelBruteMarchParity()
{
    auto shapes = sdfDefaultSceneShapes();
    SdfAccelScene accelScene;
    sdfAccelPopulateScene(accelScene, shapes);
    expectTrue(accelScene.build(), "AccelBruteMarchParityBuild");

    const SdfMarchParamsGpu params = defaultMarchParams();

    struct Case
    {
        SdfFloat3 ro;
        SdfFloat3 rd;
        const char* name;
    };

    const Case cases[] = {
        {sdfMakeFloat3(-8.0f, 0.0f, 0.0f), sdfMakeFloat3(1.0f, 0.0f, 0.0f), "AccelBruteSphereSide"},
        {sdfMakeFloat3(0.0f, 5.0f, 0.0f), sdfMakeFloat3(0.0f, -1.0f, 0.0f), "AccelBruteSphereTop"},
    };

    for (const Case& testCase : cases) {
        const SdfHit bvhHit = accelSceneRayMarch(accelScene, testCase.ro, testCase.rd, params);
        const SdfHit bruteMarchHit = accelSceneRayMarchBrute(accelScene, testCase.ro, testCase.rd, params);
        expectTrue(bvhHit.hit, testCase.name);
        expectTrue(bruteMarchHit.hit, testCase.name);
        expectNear(bvhHit.t, bruteMarchHit.t, 1.0e-2f, testCase.name);
    }
}

void testThinCylinderCoverage()
{
    const SdfFloat2 halfExtents = sdfMakeFloat2(0.02f, 5.0f);
    auto shapes = makeSingleShape(std::make_unique<CylinderSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), halfExtents));
    const CylinderSdf oracle(sdfMakeFloat3(0.0f, 0.0f, 0.0f), halfExtents);

    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "ThinCylinderBuild");

    for (float y = -5.0f; y <= 5.0f; y += 0.25f) {
        for (float a = 0.0f; a <= 6.28318f; a += 0.3f) {
            const SdfFloat3 p = sdfMakeFloat3(0.02f * cosf(a), y, 0.02f * sinf(a));
            expectNear(accelSceneEvalSDF(scene, p), oracle.evalWorld(p), 1.0e-4f, "ThinCylinderSurfaceGrid");
        }
    }

    std::mt19937 rng(0x71110C0u);
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);
    for (int i = 0; i < 500; ++i) {
        const SdfFloat3 p = sdfMakeFloat3(dist(rng), dist(rng), dist(rng));
        expectNear(accelSceneEvalSDF(scene, p), oracle.evalWorld(p), 1.0e-4f, "ThinCylinderRandom");
    }

    const SdfMarchParamsGpu params = defaultMarchParams();
    const SdfHit hit = accelSceneRayMarch(
        scene,
        sdfMakeFloat3(1.0f, 0.0f, 0.0f),
        sdfMakeFloat3(-1.0f, 0.0f, 0.0f),
        params);
    expectTrue(hit.hit, "ThinCylinderRayHit");
    expectNear(hit.t, 0.98f, 0.05f, "ThinCylinderRayTHit");
}

void testCappedConeBuild()
{
    auto shapes = makeSingleShape(std::make_unique<CappedConeSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), 1.0f, 0.5f, 0.2f));
    const CappedConeSdf oracle(sdfMakeFloat3(0.0f, 0.0f, 0.0f), 1.0f, 0.5f, 0.2f);

    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "CappedConeBuild");

    expectNear(
        accelSceneEvalSDF(scene, sdfMakeFloat3(0.0f, 0.0f, 0.0f)),
        oracle.evalWorld(sdfMakeFloat3(0.0f, 0.0f, 0.0f)),
        1.0e-4f,
        "CappedConeCenter");
}

void testTightBoundsNoPad()
{
    SdfAccelBuildParams buildParams{};
    buildParams.boundsPadding = 1.0e-4f;

    auto shapes = makeSingleShape(
        std::make_unique<CylinderSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), sdfMakeFloat2(0.02f, 5.0f)));

    SdfAccelScene scene;
    scene.setBuildParams(buildParams);
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "TightBoundsBuild");

    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    expectTrue(hostScene != nullptr && hostScene->objectCount == 1, "TightBoundsHostScene");
    const SdfAccelObjectGpu& object = hostScene->objects[0];
    expectNear(object.boundsMin.x, -0.02f, 1.0e-5f, "TightBoundsMinX");
    expectNear(object.boundsMax.x, 0.02f, 1.0e-5f, "TightBoundsMaxX");
    expectNear(object.boundsMin.y, -5.0f, 1.0e-5f, "TightBoundsMinY");
    expectNear(object.boundsMax.y, 5.0f, 1.0e-5f, "TightBoundsMaxY");
}

void testBvhBoundsMeshNonEmpty()
{
    auto shapes = makeSingleShape(std::make_unique<SphereSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), 1.0f));

    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "BvhBoundsMeshBuild");

    constexpr size_t kWireframeVerticesPerBox = 24;
    const SdfAccelBoundsMesh mesh = sdfAccelBuildBoundsMesh(scene, QColor(Qt::yellow));
    expectTrue(mesh.bvhLines.size() == kWireframeVerticesPerBox, "BvhBoundsMeshBvhLines");
}

void testManyObjects()
{
    std::vector<std::unique_ptr<SdfShape>> shapes;
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                const SdfFloat3 center = sdfMakeFloat3(
                    static_cast<float>(x) * 2.0f,
                    static_cast<float>(y) * 2.0f,
                    static_cast<float>(z) * 2.0f);
                shapes.push_back(std::make_unique<SphereSdf>(center, 0.25f));
            }
        }
    }

    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "ManyObjectsBuild");

    std::mt19937 rng(0xABCDEF01u);
    std::uniform_real_distribution<float> dist(-1.0f, 7.0f);
    for (int i = 0; i < 300; ++i) {
        const SdfFloat3 p = sdfMakeFloat3(dist(rng), dist(rng), dist(rng));
        expectNear(accelSceneEvalSDF(scene, p), bruteSceneSDF(p, shapes), 1.0e-4f, "ManyObjectsRandomParity");
    }
}

void testBuildUploadSmoke()
{
    auto shapes = sdfDefaultSceneShapes();
    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "UploadSmokeBuild");
    expectTrue(scene.allocate(), "UploadSmokeAllocate");
    expectTrue(scene.upload(0), "UploadSmokeUpload");
    expectTrue(scene.deviceScene() != nullptr, "UploadSmokeDeviceScene");
    scene.release();
}

void testTraversalBenchmarkReport()
{
    constexpr int kRayCount = 128;
    constexpr int kWarmupIters = 10;
    constexpr int kMeasureIters = 150;
    SphereGridConfig grid3x4x1{};
    grid3x4x1.nx = 3;
    grid3x4x1.ny = 4;
    grid3x4x1.nz = 1;
    runGridTraversalBenchmark(
        grid3x4x1,
        "3x4x1 spheres",
        kRayCount,
        kWarmupIters,
        kMeasureIters,
        true);

    SphereGridConfig grid3x3x3{};
    grid3x3x3.nx = 3;
    grid3x3x3.ny = 3;
    grid3x3x3.nz = 3;
    runGridTraversalBenchmark(
        grid3x3x3,
        "3x3x3 spheres",
        kRayCount,
        kWarmupIters,
        kMeasureIters,
        true);

    SphereGridConfig grid10x10x10{};
    grid10x10x10.nx = 10;
    grid10x10x10.ny = 10;
    grid10x10x10.nz = 10;
    runGridTraversalBenchmark(
        grid10x10x10,
        "10x10x10 spheres",
        kRayCount,
        kWarmupIters,
        kMeasureIters,
        true);
}

} // namespace

int main()
{
    testGpuStructLayout();
    testBuildEmptyFails();
    testSingleSphereEval();
    testDefaultLayoutParity();
    testRayMarchParity();
    testConservativeLowerBound();
    testStraddleConservativeOpenSpace();
    testAnalyticalObjectsFlagged();
    testBvhBuiltForScene();
    testAccelBruteMarchParity();
    testThinCylinderCoverage();
    testCappedConeBuild();
    testTightBoundsNoPad();
    testBvhBoundsMeshNonEmpty();
    testManyObjects();
    testBuildUploadSmoke();
    testTraversalBenchmarkReport();

    if (failureCount() == 0) {
        std::cout << "All SdfAccel tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << failureCount() << " SdfAccel test(s) failed.\n";
    return EXIT_FAILURE;
}
