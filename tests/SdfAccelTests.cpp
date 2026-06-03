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
    expectTrue(sizeof(SdfOctreeNode) == 48, "SdfOctreeNodeSize");
    expectTrue(sizeof(SdfAccelObjectGpu) == 64, "SdfAccelObjectGpuSize");
    expectTrue(sizeof(SdfAccelPayloadGpu) == 48, "SdfAccelPayloadGpuSize");
    expectTrue(sizeof(SdfBvhNode) == 48, "SdfBvhNodeSize");
    expectTrue(sizeof(SdfAccelSceneGpu) == 64, "SdfAccelSceneGpuSize");

    expectTrue(offsetof(SdfOctreeNode, dMin) == 8, "SdfOctreeNodeDMinOffset");
    expectTrue(offsetof(SdfOctreeNode, center) == 16, "SdfOctreeNodeCenterOffset");
    expectTrue(offsetof(SdfOctreeNode, halfExtent) == 28, "SdfOctreeNodeHalfExtentOffset");
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

    const SdfOctreeNode& root = hostScene->octreeNodes[object.octreeRootIndex];
    expectNear(root.halfExtent.x, 0.02f + buildParams.boundsPadding, 1.0e-4f, "TightRootHalfX");
    expectNear(root.halfExtent.y, 5.0f + buildParams.boundsPadding, 1.0e-4f, "TightRootHalfY");
    expectNear(root.halfExtent.z, 0.02f + buildParams.boundsPadding, 1.0e-4f, "TightRootHalfZ");
}

void testNoFalsePrune()
{
    auto shapes = makeSingleShape(
        std::make_unique<CylinderSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), sdfMakeFloat2(0.02f, 5.0f)));

    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "NoFalsePruneBuild");

    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    expectTrue(hostScene != nullptr && hostScene->objectCount == 1, "NoFalsePruneHostScene");
    const SdfAccelObjectGpu& object = hostScene->objects[0];
    const uint32_t rootIndex = object.octreeNodeOffset + object.octreeRootIndex;

    for (float y = -5.0f; y <= 5.0f; y += 0.1f) {
        for (float a = 0.0f; a <= 6.28318f; a += 0.15f) {
            const SdfFloat3 worldP = sdfMakeFloat3(0.02f * cosf(a), y, 0.02f * sinf(a));
            const SdfFloat3 localP = sdfSub3(worldP, object.center);
            const float d = sdCylinder(localP, sdfMakeFloat2(0.02f, 5.0f));
            if (std::abs(d) < 0.1f) {
                expectTrue(
                    octreeDescentOk(localP, hostScene->octreeNodes, rootIndex),
                    "NoFalsePruneNearSurfacePath");
            }
        }
    }
}

void testOctreeDepthCap()
{
    SdfAccelBuildParams buildParams{};
    buildParams.maxDepth = 4;

    auto shapes = makeSingleShape(
        std::make_unique<CylinderSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), sdfMakeFloat2(0.02f, 5.0f)));

    SdfAccelScene scene;
    scene.setBuildParams(buildParams);
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "OctreeDepthCapBuild");

    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    expectTrue(hostScene != nullptr, "OctreeDepthCapHostScene");
    expectTrue(hostScene->octreeNodeCount < 5000, "OctreeDepthCapNodeCount");

    const SdfOctreeNode& root = hostScene->octreeNodes[hostScene->objects[0].octreeRootIndex];
    const float minNodeSize = sdfAccelAutoMinNodeSize(root.halfExtent, buildParams.maxDepth);
    expectTrue(minNodeSize > 0.0f, "OctreeAutoMinNodeSize");
}

void testOctreeExteriorMeshCoarsestShell()
{
    SdfAccelBuildParams buildParams{};
    buildParams.maxDepth = 7;

    auto shapes = makeSingleShape(std::make_unique<SphereSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), 1.0f));

    SdfAccelScene scene;
    scene.setBuildParams(buildParams);
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "OctreeExteriorMeshBuild");

    constexpr size_t kWireframeVerticesPerBox = 24;
    const SdfAccelBoundsMesh mesh =
        sdfAccelBuildBoundsMesh(scene, QColor(Qt::white), QColor(Qt::yellow));
    expectTrue(
        mesh.octreeExteriorLines.size() == kWireframeVerticesPerBox,
        "OctreeExteriorMeshSingleBox");
    expectTrue(
        mesh.octreeLeavesLines.size() > kWireframeVerticesPerBox,
        "OctreeExteriorMeshLeavesFinerThanExterior");
}

void testOctreeExteriorStraddleFlags()
{
    SdfAccelBuildParams buildParams{};
    buildParams.maxDepth = 6;

    auto shapes = makeSingleShape(std::make_unique<SphereSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), 1.0f));

    SdfAccelScene scene;
    scene.setBuildParams(buildParams);
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "OctreeExteriorStraddleBuild");

    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    expectTrue(hostScene != nullptr && hostScene->objectCount == 1, "OctreeExteriorStraddleHostScene");

    const SdfAccelObjectGpu& object = hostScene->objects[0];
    const uint32_t nodeStart = object.octreeNodeOffset;
    const uint32_t nodeEnd = hostScene->octreeNodeCount;

    int straddleCount = 0;
    int insideSolidCount = 0;
    for (uint32_t nodeIndex = nodeStart; nodeIndex < nodeEnd; ++nodeIndex) {
        const SdfOctreeNode& node = hostScene->octreeNodes[nodeIndex];
        const uint8_t flags = sdfAccelFlagsFromPacked(node.childMaskAndFlags);
        if ((flags & SdfOctreeFlagStraddlesSurface) != 0) {
            ++straddleCount;
        }
        if ((flags & SdfOctreeFlagInsideSolid) != 0) {
            ++insideSolidCount;
        }
    }

    const int totalCount = static_cast<int>(nodeEnd - nodeStart);
    expectTrue(totalCount > 0, "OctreeExteriorStraddleTotalCount");
    expectTrue(straddleCount > 0, "OctreeExteriorStraddleNonZero");
    expectTrue(insideSolidCount > 0, "OctreeExteriorInsideSolidPresent");
    expectTrue(
        straddleCount + insideSolidCount < totalCount,
        "OctreeExteriorOutsideNodesPresent");
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

void testSurfaceLeafNodesPresent()
{
    auto shapes = makeSingleShape(std::make_unique<SphereSdf>(sdfMakeFloat3(0.0f, 0.0f, 0.0f), 1.0f));

    SdfAccelScene scene;
    sdfAccelPopulateScene(scene, shapes);
    expectTrue(scene.build(), "SurfaceLeafNodesBuild");

    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    expectTrue(hostScene != nullptr, "SurfaceLeafNodesHostScene");

    const SdfAccelObjectGpu& object = hostScene->objects[0];
    int surfaceLeafCount = 0;
    const uint32_t nodeStart = object.octreeNodeOffset;
    const uint32_t nodeEnd = hostScene->octreeNodeCount;
    for (uint32_t nodeIndex = nodeStart; nodeIndex < nodeEnd; ++nodeIndex) {
        const SdfOctreeNode& node = hostScene->octreeNodes[nodeIndex];
        if (node.firstChildIndex == 0 && sdfAccelOctreeNodeIsSurfaceLeaf(node)) {
            ++surfaceLeafCount;
        }
    }
    expectTrue(surfaceLeafCount > 0, "SurfaceLeafNodesNonZero");
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

} // namespace

int main()
{
    testGpuStructLayout();
    testBuildEmptyFails();
    testSingleSphereEval();
    testDefaultLayoutParity();
    testRayMarchParity();
    testThinCylinderCoverage();
    testCappedConeBuild();
    testTightBoundsNoPad();
    testNoFalsePrune();
    testOctreeDepthCap();
    testOctreeExteriorMeshCoarsestShell();
    testOctreeExteriorStraddleFlags();
    testManyObjects();
    testSurfaceLeafNodesPresent();
    testBuildUploadSmoke();

    if (failureCount() == 0) {
        std::cout << "All SdfAccel tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << failureCount() << " SdfAccel test(s) failed.\n";
    return EXIT_FAILURE;
}
