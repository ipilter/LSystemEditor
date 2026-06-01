#include "SdfTestHelpers.h"

#include <cmath>
#include <iostream>
#include <random>
#include <string>

using namespace SdfTest;

namespace {

void testSdCylinderSanity()
{
    const SdfFloat2 h = sdfMakeFloat2(0.5f, 1.0f);

    expectNear(sdCylinder(sdfMakeFloat3(0.5f, 0.0f, 0.0f), h), 0.0f, 1.0e-5f, "SdCylinderOnSideSurface");
    expectNear(sdCylinder(sdfMakeFloat3(0.0f, 0.0f, 0.0f), h), -0.5f, 1.0e-5f, "SdCylinderDeepInside");
    expectTrue(sdCylinder(sdfMakeFloat3(100.0f, 100.0f, 100.0f), h) > 0.0f, "SdCylinderFarOutside");
    expectNear(sdCylinder(sdfMakeFloat3(0.0f, 1.0f, 0.0f), h), 0.0f, 1.0e-5f, "SdCylinderCapPlane");
    expectTrue(sdCylinder(sdfMakeFloat3(0.1f, 0.1f, 0.1f), h) < 0.0f, "SdCylinderOffCenterInside");
}

void testSceneSDFUsesCenter()
{
    SdfSceneGpu scene = defaultScene();
    scene.cylinderCenter = sdfMakeFloat3(2.0f, 0.0f, 0.0f);

    expectNear(sceneSDF(sdfMakeFloat3(2.5f, 0.0f, 0.0f), &scene), 0.0f, 1.0e-5f, "SceneSDFCenteredSurface");
}

void testAnalyticMarchParity()
{
    const SdfSceneGpu scene = defaultScene();
    const SdfMarchParamsGpu params = defaultMarchParams();

    struct Case
    {
        SdfFloat3 ro;
        SdfFloat3 rd;
        bool expectHit;
        const char* name;
    };

    const Case cases[] = {
        {sdfMakeFloat3(-5.0f, 0.0f, 0.3f), sdfMakeFloat3(1.0f, 0.0f, 0.0f), true, "AxialBodyHit"},
        {sdfMakeFloat3(0.2f, 5.0f, 0.2f), sdfMakeFloat3(0.0f, -1.0f, 0.0f), true, "CapHit"},
        {sdfMakeFloat3(-3.0f, -0.4f, -0.2f), sdfNormalize3(sdfMakeFloat3(1.0f, 0.2f, 0.1f)), true, "ObliqueBodyHit"},
        {sdfMakeFloat3(-5.0f, 0.0f, 0.51f), sdfMakeFloat3(1.0f, 0.0f, 0.0f), false, "GrazingMiss"},
        {sdfMakeFloat3(-5.0f, 0.0f, 0.0f), sdfMakeFloat3(0.0f, 1.0f, 0.0f), false, "FarMiss"},
    };

    for (const Case& testCase : cases) {
        const SdfHit march = sdfRayMarch(testCase.ro, testCase.rd, &scene, &params);
        const AnalyticHit analytic =
            analyticCylinderHit(testCase.ro, testCase.rd, scene.cylinderCenter, scene.cylinderHalfExtents, params.maxDistance);

        expectTrue(march.hit == testCase.expectHit, testCase.name);
        expectTrue(analytic.hit == testCase.expectHit, testCase.name);
        if (testCase.expectHit) {
            expectNear(march.t, analytic.t, 1.0e-3f, testCase.name);
            expectHitInvariants(testCase.ro, testCase.rd, march, &scene, &params, testCase.name);
        }
    }
}

void testInsideStartIsMiss()
{
    const SdfSceneGpu scene = defaultScene();
    const SdfMarchParamsGpu params = defaultMarchParams();

    const SdfHit march = sdfRayMarch(sdfMakeFloat3(0.0f, 0.0f, 0.0f), sdfMakeFloat3(1.0f, 0.0f, 0.0f), &scene, &params);
    expectTrue(!march.hit, "InsideStartMiss");
}

void testBruteForceOracle()
{
    const SdfSceneGpu scene = defaultScene();
    const SdfMarchParamsGpu params = defaultMarchParams();
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_real_distribution<float> dist(-4.0f, 4.0f);

    for (int i = 0; i < 200; ++i) {
        SdfFloat3 ro = sdfMakeFloat3(dist(rng), dist(rng), dist(rng));
        SdfFloat3 rd = sdfNormalize3(sdfMakeFloat3(dist(rng), dist(rng), dist(rng)));
        if (!isOutsideStart(ro, &scene, params.surfaceEpsilon)) {
            continue;
        }

        const SdfHit march = sdfRayMarch(ro, rd, &scene, &params);
        const SdfHit reference = bruteForceMarch(ro, rd, &scene, &params);

        if (reference.hit) {
            expectTrue(march.hit, "BruteForceOracleHit");
            expectNear(march.t, reference.t, params.surfaceEpsilon * 50.0f, "BruteForceOracleDistance");
            expectHitInvariants(ro, rd, march, &scene, &params, "BruteForceOracleInvariants");
        } else {
            expectTrue(!march.hit, "BruteForceOracleMiss");
        }
    }
}

void testCapBodyGrid()
{
    const SdfSceneGpu scene = defaultScene();
    const SdfMarchParamsGpu params = defaultMarchParams();
    const SdfFloat2 h = scene.cylinderHalfExtents;

    for (int azimuth = 0; azimuth < 36; ++azimuth) {
        const float angle = static_cast<float>(azimuth) * (6.2831853f / 36.0f);
        const SdfFloat3 target = sdfMakeFloat3(h.x * std::cos(angle), h.y, h.x * std::sin(angle));
        const SdfFloat3 ro = sdfAdd3(target, sdfMakeFloat3(0.0f, 2.0f, 0.0f));
        const SdfFloat3 rd = sdfNormalize3(sdfSub3(target, ro));

        const SdfHit march = sdfRayMarch(ro, rd, &scene, &params);
        const AnalyticHit analytic =
            analyticCylinderHit(ro, rd, scene.cylinderCenter, scene.cylinderHalfExtents, params.maxDistance);

        expectTrue(march.hit, "CapBodyGridHit");
        if (analytic.hit) {
            expectNear(march.t, analytic.t, 1.0e-3f, "CapBodyGridDistance");
        }
        expectHitInvariants(ro, rd, march, &scene, &params, "CapBodyGridInvariants");
    }
}

void testSilhouetteAndShallowCap()
{
    const SdfSceneGpu scene = defaultScene();
    const SdfMarchParamsGpu params = defaultMarchParams();
    const float r = scene.cylinderHalfExtents.x;

    for (int i = 0; i < 20; ++i) {
        const float y = -0.8f + static_cast<float>(i) * 0.08f;
        const SdfFloat3 ro = sdfMakeFloat3(-6.0f, y, r + 0.02f + 0.001f * static_cast<float>(i));
        const SdfFloat3 rd = sdfMakeFloat3(1.0f, 0.0f, 0.0f);
        const SdfHit march = sdfRayMarch(ro, rd, &scene, &params);
        expectTrue(!march.hit, "SilhouetteGrazingMiss");
    }

    const SdfFloat3 shallowRo = sdfMakeFloat3(0.1f, 4.0f, 0.1f);
    const SdfFloat3 shallowRd = sdfNormalize3(sdfMakeFloat3(0.05f, -1.0f, 0.02f));
    const SdfHit shallowHit = sdfRayMarch(shallowRo, shallowRd, &scene, &params);
    expectTrue(shallowHit.hit, "ShallowCapHit");
    expectHitInvariants(shallowRo, shallowRd, shallowHit, &scene, &params, "ShallowCapInvariants");
}

void testParameterSensitivity()
{
    const SdfSceneGpu scene = defaultScene();
    SdfMarchParamsGpu params = defaultMarchParams();
    const SdfFloat3 ro = sdfMakeFloat3(-5.0f, 0.0f, 0.3f);
    const SdfFloat3 rd = sdfMakeFloat3(1.0f, 0.0f, 0.0f);

    params.maxSteps = 4;
    const SdfHit lowSteps = sdfRayMarch(ro, rd, &scene, &params);
    expectTrue(!lowSteps.hit, "LowMaxStepsMiss");

    params = defaultMarchParams();
    params.surfaceEpsilon = 1.0e-3f;
    const SdfHit coarseEpsilon = sdfRayMarch(ro, rd, &scene, &params);
    expectTrue(coarseEpsilon.hit, "CoarseEpsilonHit");
    expectHitInvariants(ro, rd, coarseEpsilon, &scene, &params, "CoarseEpsilonInvariants");

    params = defaultMarchParams();
    const SdfHit unitDir = sdfRayMarch(ro, rd, &scene, &params);
    const SdfHit scaledDir = sdfRayMarch(ro, sdfScale3(rd, 2.0f), &scene, &params);
    expectTrue(unitDir.hit && scaledDir.hit, "NonUnitDirectionHit");
    expectNear(unitDir.t, scaledDir.t, 1.0e-5f, "NonUnitDirectionSameT");
}

void testDeterminism()
{
    const SdfSceneGpu scene = defaultScene();
    const SdfMarchParamsGpu params = defaultMarchParams();
    const SdfFloat3 ro = sdfMakeFloat3(-2.0f, 0.4f, 0.2f);
    const SdfFloat3 rd = sdfNormalize3(sdfMakeFloat3(1.0f, -0.1f, 0.05f));

    const SdfHit first = sdfRayMarch(ro, rd, &scene, &params);
    const SdfHit second = sdfRayMarch(ro, rd, &scene, &params);

    expectTrue(first.hit == second.hit, "DeterminismHitFlag");
    expectNear(first.t, second.t, 1.0e-7f, "DeterminismT");
    expectTrue(first.steps == second.steps, "DeterminismSteps");
    expectNear(first.sdfAtHit, second.sdfAtHit, 1.0e-7f, "DeterminismSdfAtHit");
}

void testMissRecordsSteps()
{
    const SdfSceneGpu scene = defaultScene();
    SdfMarchParamsGpu params = defaultMarchParams();
    params.maxDistance = 20.0f;

    const SdfFloat3 ro = sdfMakeFloat3(-5.0f, 0.0f, 0.55f);
    const SdfFloat3 rd = sdfMakeFloat3(1.0f, 0.0f, 0.0f);
    const SdfHit miss = sdfRayMarch(ro, rd, &scene, &params);
    expectTrue(!miss.hit, "MissRecordsStepsNotHit");
    expectTrue(miss.steps > 0, "MissRecordsStepsCount");

    params.maxSteps = 3;
    params.maxDistance = 100.0f;
    const SdfHit exhausted = sdfRayMarch(sdfMakeFloat3(-5.0f, 0.0f, 0.3f), rd, &scene, &params);
    expectTrue(!exhausted.hit, "ExhaustedStepsMiss");
    expectTrue(exhausted.steps == params.maxSteps, "ExhaustedStepsCount");
}

void testStepsToHeatmapSanity()
{
    SdfMarchParamsGpu params{};
    params.backgroundR = 0.25f;
    params.backgroundG = 0.5f;
    params.backgroundB = 0.75f;

    const SdfFloat3 low = stepsToHeatmap(1, 256, true, &params);
    const SdfFloat3 high = stepsToHeatmap(200, 256, true, &params);
    expectTrue(low.x < high.x || low.y < high.y, "StepsToHeatmapLowVsHigh");

    const SdfFloat3 missNoSteps = stepsToHeatmap(0, 256, false, &params);
    const SdfFloat3 missWithSteps = stepsToHeatmap(32, 256, false, &params);
    expectNear(missNoSteps.x, params.backgroundR, 1.0e-5f, "StepsToHeatmapMissBackgroundR");
    expectNear(missNoSteps.y, params.backgroundG, 1.0e-5f, "StepsToHeatmapMissBackgroundG");
    expectNear(missNoSteps.z, params.backgroundB, 1.0e-5f, "StepsToHeatmapMissBackgroundB");
    expectTrue(missWithSteps.x > missNoSteps.x || missWithSteps.y > missNoSteps.y, "StepsToHeatmapMissNearGeometry");

    const SdfFloat3 normalColor = normalToColor(sdfMakeFloat3(0.0f, 1.0f, 0.0f), true, &params);
    expectNear(normalColor.x, 0.5f, 1.0e-5f, "NormalToColorX");
    expectNear(normalColor.y, 1.0f, 1.0e-5f, "NormalToColorY");
    expectNear(normalColor.z, 0.5f, 1.0e-5f, "NormalToColorZ");
}

void testConeReferenceParity()
{
    const float h = 1.0f;
    const float r1 = 0.5f;
    const float r2 = 0.125f;

    const SdfFloat3 samples[] = {
        sdfMakeFloat3(0.0f, h, 0.0f),
        sdfMakeFloat3(r2, h, 0.0f),
        sdfMakeFloat3(0.0f, -h, 0.0f),
        sdfMakeFloat3(r1, -h, 0.0f),
        sdfMakeFloat3(0.3125f, 0.0f, 0.0f),
    };

    for (const SdfFloat3& localP : samples) {
        const float current = sdCappedCone(localP, h, r1, r2);
        const float reference = sdCappedConeReference(localP, h, r1, r2);
        expectNear(current, reference, 1.0e-4f, "ConeReferenceParity");
    }

    for (int azimuth = 0; azimuth < 12; ++azimuth) {
        const float angle = static_cast<float>(azimuth) * (6.2831853f / 12.0f);
        const SdfFloat3 localP = sdfMakeFloat3(r2 * std::cos(angle), h, r2 * std::sin(angle));
        const float current = sdCappedCone(localP, h, r1, r2);
        const float reference = sdCappedConeReference(localP, h, r1, r2);
        expectNear(current, reference, 1.0e-4f, "ConeReferenceAzimuthGrid");
    }

    for (int ix = -4; ix <= 4; ++ix) {
        for (int iy = -4; iy <= 4; ++iy) {
            const SdfFloat3 localP = sdfMakeFloat3(
                static_cast<float>(ix) * 0.12f,
                static_cast<float>(iy) * 0.25f,
                0.0f);
            const float current = sdCappedCone(localP, h, r1, r2);
            const float reference = sdCappedConeReference(localP, h, r1, r2);
            expectNear(current, reference, 1.0e-4f, "ConeReferenceOffSurfaceGrid");
        }
    }
}

void testConeOnSurfaceSanity()
{
    const SdfSceneGpu scene = coneOnlyScene();
    const float h = scene.coneHalfHeight;
    const float r1 = scene.coneRadiusBottom;
    const float r2 = scene.coneRadiusTop;

    const SdfFloat3 localSamples[] = {
        sdfMakeFloat3(0.0f, h, 0.0f),
        sdfMakeFloat3(r2 * 0.8f, h, 0.0f),
        sdfMakeFloat3(0.0f, -h, 0.0f),
        sdfMakeFloat3(r1 * 0.9f, -h, 0.0f),
        sdfMakeFloat3(0.3125f, 0.0f, 0.0f),
    };

    for (const SdfFloat3& localP : localSamples) {
        const float d = evalConeSDF(worldConePoint(&scene, localP), &scene);
        expectNear(d, 0.0f, 1.0e-3f, "ConeOnSurfaceSanity");
    }
}

void testConeCameraGridMarch()
{
    const SdfSceneGpu scene = coneOnlyScene();
    const SdfMarchParamsGpu params = defaultMarchParams();
    const SdfFloat3 coneTarget = scene.coneCenter;

    for (int ix = -2; ix <= 2; ++ix) {
        for (int iy = -2; iy <= 2; ++iy) {
            const SdfFloat3 ro = sdfMakeFloat3(
                static_cast<float>(ix) * 0.15f,
                0.5f + static_cast<float>(iy) * 0.1f,
                4.0f);
            const SdfFloat3 target = sdfMakeFloat3(
                coneTarget.x,
                static_cast<float>(iy) * 0.05f,
                coneTarget.z);
            const SdfFloat3 rd = sdfNormalize3(sdfSub3(target, ro));

            if (!isOutsideStart(ro, &scene, params.surfaceEpsilon)) {
                continue;
            }

            const SdfHit march = sdfRayMarch(ro, rd, &scene, &params);
            const SdfHit reference = bruteForceMarch(ro, rd, &scene, &params);

            if (reference.hit) {
                expectTrue(march.hit, "ConeCameraGridHit");
                if (!march.hit) {
                    std::cerr << "  miss steps=" << march.steps << " maxSteps=" << params.maxSteps << '\n';
                }
                expectNear(march.t, reference.t, params.surfaceEpsilon * 50.0f, "ConeCameraGridDistance");
                expectHitInvariants(ro, rd, march, &scene, &params, "ConeCameraGridInvariants");
            }
        }
    }

    const SdfFloat3 capCenterRo = sdfMakeFloat3(0.0f, 0.5f, 4.0f);
    const SdfFloat3 capCenterTarget = sdfMakeFloat3(coneTarget.x, scene.coneHalfHeight, coneTarget.z);
    const SdfFloat3 capCenterRd = sdfNormalize3(sdfSub3(capCenterTarget, capCenterRo));
    const SdfHit capCenterMarch = sdfRayMarch(capCenterRo, capCenterRd, &scene, &params);
    const SdfHit capCenterReference = bruteForceMarch(capCenterRo, capCenterRd, &scene, &params);
    expectTrue(capCenterReference.hit, "ConeCapCenterReferenceHit");
    expectTrue(capCenterMarch.hit, "ConeCapCenterMarchHit");
    if (!capCenterMarch.hit) {
        std::cerr << "  cap-center miss steps=" << capCenterMarch.steps << " maxSteps=" << params.maxSteps << '\n';
    }
    expectHitInvariants(capCenterRo, capCenterRd, capCenterMarch, &scene, &params, "ConeCapCenterInvariants");
}

void testDemoSceneConeSmoke()
{
    const SdfSceneGpu scene = demoScene();
    const SdfMarchParamsGpu params = defaultMarchParams();

    const SdfFloat3 capCenterRo = sdfMakeFloat3(0.0f, 0.5f, 4.0f);
    const SdfFloat3 capCenterTarget = sdfMakeFloat3(scene.coneCenter.x, scene.coneHalfHeight, scene.coneCenter.z);
    const SdfFloat3 capCenterRd = sdfNormalize3(sdfSub3(capCenterTarget, capCenterRo));

    const SdfHit march = sdfRayMarch(capCenterRo, capCenterRd, &scene, &params);
    const SdfHit reference = bruteForceMarch(capCenterRo, capCenterRd, &scene, &params);

    expectTrue(reference.hit, "DemoSceneConeReferenceHit");
    expectTrue(march.hit, "DemoSceneConeMarchHit");
    expectHitInvariants(capCenterRo, capCenterRd, march, &scene, &params, "DemoSceneConeInvariants");
}

} // namespace

int main()
{
    testSdCylinderSanity();
    testSceneSDFUsesCenter();
    testAnalyticMarchParity();
    testInsideStartIsMiss();
    testBruteForceOracle();
    testCapBodyGrid();
    testSilhouetteAndShallowCap();
    testParameterSensitivity();
    testDeterminism();
    testMissRecordsSteps();
    testStepsToHeatmapSanity();
    testConeReferenceParity();
    testConeOnSurfaceSanity();
    testConeCameraGridMarch();
    testDemoSceneConeSmoke();

    if (failureCount() == 0) {
        std::cout << "All SDF ray marcher tests passed.\n";
        return 0;
    }

    std::cerr << failureCount() << " SDF ray marcher test(s) failed.\n";
    return 1;
}
