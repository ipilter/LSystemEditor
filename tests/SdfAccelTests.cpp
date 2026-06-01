#include "SdfAccelTestHelpers.h"
#include "SdfAccelBoundsCore.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <random>

using namespace SdfAccelTest;

namespace {

void testGpuStructLayout()
{
    expectTrue(sizeof(SdfOctreeNode) == 48, "SdfOctreeNodeSize");
    expectTrue(sizeof(SdfAccelObjectGpu) == 64, "SdfAccelObjectGpuSize");
    expectTrue(sizeof(SdfAccelPayloadGpu) == 48, "SdfAccelPayloadGpuSize");
    expectTrue(sizeof(SdfBvhNode) == 48, "SdfBvhNodeSize");
    expectTrue(sizeof(SdfAccelSceneGpu) == 64, "SdfAccelSceneGpuSize");

    expectTrue(offsetof(SdfOctreeNode, dMin) == 8, "SdfOctreeNodeDMinOffset");
    expectTrue(offsetof(SdfOctreeNode, center) == 16, "SdfOctreeNodeCenterOffset");
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
    SdfAccelScene scene;
    scene.addSphere(sdfMakeFloat3(1.0f, 2.0f, 3.0f), 0.5f);
    expectTrue(scene.build(), "SingleSphereBuild");

    const BruteObject object{SdfAccelPrimitiveType::Sphere, sdfMakeFloat3(1.0f, 2.0f, 3.0f), 0.5f};
    expectNear(scene.evalSDF(sdfMakeFloat3(1.5f, 2.0f, 3.0f)), bruteEvalObject(sdfMakeFloat3(1.5f, 2.0f, 3.0f), object), 1.0e-5f, "SingleSphereSurface");
    expectNear(scene.evalSDF(sdfMakeFloat3(1.0f, 2.0f, 3.0f)), bruteEvalObject(sdfMakeFloat3(1.0f, 2.0f, 3.0f), object), 1.0e-5f, "SingleSphereCenter");
    expectTrue(scene.evalSDF(sdfMakeFloat3(10.0f, 10.0f, 10.0f)) > 0.0f, "SingleSphereOutside");
}

void testDefaultLayoutParity()
{
    SdfAccelScene scene;
    scene.setDefaultLayout();
    expectTrue(scene.build(), "DefaultLayoutBuild");

    const std::vector<BruteObject> objects = defaultLayoutObjects();
    for (float x = -4.0f; x <= 4.0f; x += 0.5f) {
        for (float y = -2.0f; y <= 2.0f; y += 0.5f) {
            for (float z = -2.0f; z <= 2.0f; z += 0.5f) {
                const SdfFloat3 p = sdfMakeFloat3(x, y, z);
                expectNear(
                    scene.evalSDF(p),
                    bruteSceneSDF(p, objects),
                    1.0e-4f,
                    "DefaultLayoutGridParity");
            }
        }
    }
}

void testRayMarchParity()
{
    SdfAccelScene accelScene;
    accelScene.setDefaultLayout();
    expectTrue(accelScene.build(), "RayMarchParityBuild");

    const SdfSceneGpu legacyScene = defaultLayoutLegacyScene();
    const SdfMarchParamsGpu params = defaultMarchParams();

    struct Case
    {
        SdfFloat3 ro;
        SdfFloat3 rd;
        bool expectHit;
        const char* name;
    };

    const Case cases[] = {
        {sdfMakeFloat3(-8.0f, 0.0f, 0.0f), sdfMakeFloat3(1.0f, 0.0f, 0.0f), true, "CylinderHit"},
        {sdfMakeFloat3(0.0f, 5.0f, 0.0f), sdfMakeFloat3(0.0f, -1.0f, 0.0f), true, "SphereCapHit"},
        {sdfMakeFloat3(8.0f, 0.0f, 0.0f), sdfMakeFloat3(-1.0f, 0.0f, 0.0f), true, "ConeHit"},
        {sdfMakeFloat3(-8.0f, 0.0f, 0.6f), sdfMakeFloat3(1.0f, 0.0f, 0.0f), false, "CylinderMiss"},
    };

    for (const Case& testCase : cases) {
        const SdfHit accelHit = accelScene.rayMarch(testCase.ro, testCase.rd, params);
        const SdfHit legacyHit = sdfRayMarch(testCase.ro, testCase.rd, &legacyScene, &params);
        expectTrue(accelHit.hit == testCase.expectHit, testCase.name);
        expectTrue(legacyHit.hit == testCase.expectHit, testCase.name);
        if (testCase.expectHit) {
            expectNear(accelHit.t, legacyHit.t, 1.0e-2f, testCase.name);
        }
    }
}

void testThinCylinderCoverage()
{
    SdfAccelScene scene;
    scene.addCylinder(sdfMakeFloat3(0.0f, 0.0f, 0.0f), sdfMakeFloat2(0.02f, 5.0f));
    expectTrue(scene.build(), "ThinCylinderBuild");

    const BruteObject object{
        SdfAccelPrimitiveType::Cylinder,
        sdfMakeFloat3(0.0f, 0.0f, 0.0f),
        0.0f,
        0.0f,
        0.0f,
        sdfMakeFloat2(0.02f, 5.0f)};

    for (float y = -5.0f; y <= 5.0f; y += 0.25f) {
        for (float a = 0.0f; a <= 6.28318f; a += 0.3f) {
            const SdfFloat3 p = sdfMakeFloat3(0.02f * cosf(a), y, 0.02f * sinf(a));
            expectNear(scene.evalSDF(p), bruteEvalObject(p, object), 1.0e-4f, "ThinCylinderSurfaceGrid");
        }
    }

    std::mt19937 rng(0x71110C0u);
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);
    for (int i = 0; i < 500; ++i) {
        const SdfFloat3 p = sdfMakeFloat3(dist(rng), dist(rng), dist(rng));
        expectNear(scene.evalSDF(p), bruteEvalObject(p, object), 1.0e-4f, "ThinCylinderRandom");
    }

    const SdfMarchParamsGpu params = defaultMarchParams();
    const SdfHit hit = scene.rayMarch(sdfMakeFloat3(1.0f, 0.0f, 0.0f), sdfMakeFloat3(-1.0f, 0.0f, 0.0f), params);
    expectTrue(hit.hit, "ThinCylinderRayHit");
    expectNear(hit.t, 0.98f, 0.05f, "ThinCylinderRayTHit");
}

void testTightBoundsNoPad()
{
    SdfAccelBuildParams buildParams{};
    buildParams.boundsPadding = 1.0e-4f;

    SdfAccelScene scene;
    scene.setBuildParams(buildParams);
    scene.addCylinder(sdfMakeFloat3(0.0f, 0.0f, 0.0f), sdfMakeFloat2(0.02f, 5.0f));
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
    SdfAccelScene scene;
    scene.addCylinder(sdfMakeFloat3(0.0f, 0.0f, 0.0f), sdfMakeFloat2(0.02f, 5.0f));
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

    SdfAccelScene scene;
    scene.setBuildParams(buildParams);
    scene.addCylinder(sdfMakeFloat3(0.0f, 0.0f, 0.0f), sdfMakeFloat2(0.02f, 5.0f));
    expectTrue(scene.build(), "OctreeDepthCapBuild");

    const SdfAccelSceneGpu* hostScene = scene.hostScene();
    expectTrue(hostScene != nullptr, "OctreeDepthCapHostScene");
    expectTrue(hostScene->octreeNodeCount < 5000, "OctreeDepthCapNodeCount");

    const SdfOctreeNode& root = hostScene->octreeNodes[hostScene->objects[0].octreeRootIndex];
    const float minNodeSize = sdfAccelAutoMinNodeSize(root.halfExtent, buildParams.maxDepth);
    expectTrue(minNodeSize > 0.0f, "OctreeAutoMinNodeSize");
}

void testManyObjects()
{
    SdfAccelScene scene;
    std::vector<BruteObject> bruteObjects;
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                const SdfFloat3 center = sdfMakeFloat3(
                    static_cast<float>(x) * 2.0f,
                    static_cast<float>(y) * 2.0f,
                    static_cast<float>(z) * 2.0f);
                scene.addSphere(center, 0.25f);
                bruteObjects.push_back(BruteObject{SdfAccelPrimitiveType::Sphere, center, 0.25f});
            }
        }
    }
    expectTrue(scene.build(), "ManyObjectsBuild");

    std::mt19937 rng(0xABCDEF01u);
    std::uniform_real_distribution<float> dist(-1.0f, 7.0f);
    for (int i = 0; i < 300; ++i) {
        const SdfFloat3 p = sdfMakeFloat3(dist(rng), dist(rng), dist(rng));
        expectNear(scene.evalSDF(p), bruteSceneSDF(p, bruteObjects), 1.0e-4f, "ManyObjectsRandomParity");
    }
}

void testBuildUploadSmoke()
{
    SdfAccelScene scene;
    scene.setDefaultLayout();
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
    testTightBoundsNoPad();
    testNoFalsePrune();
    testOctreeDepthCap();
    testManyObjects();
    testBuildUploadSmoke();

    if (failureCount() == 0) {
        std::cout << "All SdfAccel tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << failureCount() << " SdfAccel test(s) failed.\n";
    return EXIT_FAILURE;
}
