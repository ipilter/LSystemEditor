#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelIntersectCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "QmcSamplerCore.h"
#include "Sampling/BsdfCore.h"
#include "Sampling/LightSamplingCore.h"
#include "Sampling/MisCore.h"

#include <cmath>

#if defined(__CUDACC__)
#define PATH_INTEGRATOR_FN __host__ __device__ inline
#else
#define PATH_INTEGRATOR_FN inline
#endif

namespace PathIntegratorDetail {

constexpr float kRayTMax = 1.0e6f;
constexpr float kShadowBias = 1.0e-4f;
constexpr float kMinPdf = 1.0e-8f;
constexpr int kDimsPerBounce = 12;
constexpr int kBasePathDim = 2;
constexpr int kRussianRouletteStartDepth = 3;
constexpr float kMaxRussianRouletteProb = 0.95f;

} // namespace PathIntegratorDetail

PATH_INTEGRATOR_FN int pathIntegratorDimBaseForBounce(int bounceIndex)
{
    return PathIntegratorDetail::kBasePathDim + bounceIndex * PathIntegratorDetail::kDimsPerBounce;
}

PATH_INTEGRATOR_FN float pathIntegratorNext1D(
    SampleContext& ctx,
    int dimension,
    const uint32_t* sobolMatrices,
    int sobolDimensionCount)
{
    ctx.dimension = dimension;
    return qmcNext1D(ctx, sobolMatrices, sobolDimensionCount);
}

PATH_INTEGRATOR_FN Vec3 pathIntegratorEmission(const MaterialGpu& material)
{
    const Vec3 baseColor = bsdfBaseColor(material);
    return vecMake3(
        baseColor.x * material.emission,
        baseColor.y * material.emission,
        baseColor.z * material.emission);
}

PATH_INTEGRATOR_FN Vec3 pathIntegratorEvaluateNee(
    Vec3 position,
    Vec3 normal,
    Vec3 wo,
    Vec3 wi,
    const MaterialGpu& material,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    const EnvironmentMapGpu* env,
    LightType lightType,
    float lightPdf,
    float bsdfPdfForMis)
{
    if (lightPdf <= PathIntegratorDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    if (lightIsOccluded(position, normal, wi, scene)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    Vec3 lightRadiance = vecMake3(0.0f, 0.0f, 0.0f);
    switch (lightType) {
    case LightType::Sun:
        lightRadiance = lightEvalSunRadiance(params, wi);
        break;
    case LightType::Environment:
        lightRadiance = lightEvalEnvironment(env, wi);
        break;
    case LightType::Emissive:
        break;
    }

    const float cosTheta = vecMax2(0.0f, vecDot3(normal, wi));
    if (cosTheta <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const Vec3 bsdf = bsdfEval(normal, wi, wo, material);
    const float misWeight = misBalanceWeight(lightPdf, bsdfPdfForMis);
    const float scale = misWeight * cosTheta / lightPdf;
    return vecMake3(bsdf.x * lightRadiance.x * scale, bsdf.y * lightRadiance.y * scale, bsdf.z * lightRadiance.z * scale);
}

PATH_INTEGRATOR_FN Vec3 pathIntegratorEvaluateEmissiveNee(
    Vec3 position,
    Vec3 normal,
    Vec3 wo,
    Vec3 wi,
    const MaterialGpu& material,
    const MeshAccelSceneGpu* scene,
    Vec3 lightRadiance,
    float lightPdf,
    float bsdfPdfForMis)
{
    if (lightPdf <= PathIntegratorDetail::kMinPdf) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }
    if (lightIsOccluded(position, normal, wi, scene)) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const float cosTheta = vecMax2(0.0f, vecDot3(normal, wi));
    if (cosTheta <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const Vec3 bsdf = bsdfEval(normal, wi, wo, material);
    const float misWeight = misBalanceWeight(lightPdf, bsdfPdfForMis);
    const float scale = misWeight * cosTheta / lightPdf;
    return vecMake3(
        bsdf.x * lightRadiance.x * scale,
        bsdf.y * lightRadiance.y * scale,
        bsdf.z * lightRadiance.z * scale);
}

PATH_INTEGRATOR_FN Vec3 pathIntegratorSampleDirectLighting(
    Vec3 position,
    Vec3 normal,
    Vec3 wo,
    const MaterialGpu& material,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    const EnvironmentMapGpu* env,
    SampleContext& ctx,
    int dimBase,
    const uint32_t* sobolMatrices,
    int sobolDimensionCount)
{
    const float lightChoice = pathIntegratorNext1D(ctx, dimBase, sobolMatrices, sobolDimensionCount);
    const float sunWeight = 1.0f;
    const float envWeight = (env != nullptr && env->valid != 0) ? 1.0f : 0.0f;
    const float emissiveWeight = scene != nullptr && scene->emissiveTriangleCount > 0 ? 1.0f : 0.0f;
    const float totalWeight = sunWeight + envWeight + emissiveWeight;
    if (totalWeight <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    float selector = lightChoice * totalWeight;
    Vec3 contribution = vecMake3(0.0f, 0.0f, 0.0f);
    const float diffuseWeight = (1.0f - material.metallic) * (1.0f - material.transmission);
    const float specWeight = vecMax2(0.05f, 1.0f - diffuseWeight);

    if (selector < sunWeight) {
        float pdf = 0.0f;
        const float u1 = pathIntegratorNext1D(ctx, dimBase + 1, sobolMatrices, sobolDimensionCount);
        const float u2 = pathIntegratorNext1D(ctx, dimBase + 2, sobolMatrices, sobolDimensionCount);
        const Vec3 wi = lightSampleSunDirection(params, u1, u2, pdf);
        const float bsdfPdfValue = bsdfPdf(normal, wi, wo, material, diffuseWeight, specWeight);
        contribution = pathIntegratorEvaluateNee(
            position, normal, wo, wi, material, scene, params, env, LightType::Sun, pdf, bsdfPdfValue);
    } else {
        selector -= sunWeight;
        if (selector < envWeight) {
            float pdf = 0.0f;
            const float u1 = pathIntegratorNext1D(ctx, dimBase + 3, sobolMatrices, sobolDimensionCount);
            const float u2 = pathIntegratorNext1D(ctx, dimBase + 4, sobolMatrices, sobolDimensionCount);
            const Vec3 wi = lightSampleEnvironment(env, u1, u2, pdf);
            const float bsdfPdfValue = bsdfPdf(normal, wi, wo, material, diffuseWeight, specWeight);
            contribution = pathIntegratorEvaluateNee(
                position, normal, wo, wi, material, scene, params, env, LightType::Environment, pdf, bsdfPdfValue);
        } else if (emissiveWeight > 0.0f) {
            Vec3 lightRadiance{};
            float pdf = 0.0f;
            Vec3 wi{};
            const float uPick = pathIntegratorNext1D(ctx, dimBase + 5, sobolMatrices, sobolDimensionCount);
            const float uBary = pathIntegratorNext1D(ctx, dimBase + 6, sobolMatrices, sobolDimensionCount);
            const float vBary = pathIntegratorNext1D(ctx, dimBase + 7, sobolMatrices, sobolDimensionCount);
            if (lightSampleEmissiveTriangle(scene, uPick, uBary, vBary, position, wi, lightRadiance, pdf)) {
                const float bsdfPdfValue = bsdfPdf(normal, wi, wo, material, diffuseWeight, specWeight);
                contribution = pathIntegratorEvaluateEmissiveNee(
                    position, normal, wo, wi, material, scene, lightRadiance, pdf, bsdfPdfValue);
            }
        }
    }

    return contribution;
}

PATH_INTEGRATOR_FN Vec3 tracePathFromHit(
    const MeshHit& hit,
    Vec3 rayOrigin,
    Vec3 rayDir,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    const EnvironmentMapGpu* env,
    SampleContext& ctx,
    const uint32_t* sobolMatrices,
    int sobolDimensionCount)
{
    if (!hit.hit) {
        return lightEvalEnvironmentOrBackground(env, params, rayDir);
    }

    int maxDepth = 8;
    if (params != nullptr && params->maxPathDepth > 0) {
        maxDepth = params->maxPathDepth;
    }

    Vec3 radiance = vecMake3(0.0f, 0.0f, 0.0f);
    Vec3 throughput = vecMake3(1.0f, 1.0f, 1.0f);
    Vec3 currentOrigin = rayOrigin;
    Vec3 currentDir = rayDir;
    MeshHit currentHit = hit;
    bool insideMedium = false;

    for (int depth = 0; depth < maxDepth; ++depth) {
        const Vec3 position = vecEvalRay(currentOrigin, currentDir, currentHit.t);
        const Vec3 normal = currentHit.normal;
        const Vec3 wo = vecScale3(currentDir, -1.0f);
        const MaterialGpu material = lightFetchMaterial(scene, currentHit.triangleIndex);

        radiance = vecAdd3(radiance, vecMake3(
            throughput.x * pathIntegratorEmission(material).x,
            throughput.y * pathIntegratorEmission(material).y,
            throughput.z * pathIntegratorEmission(material).z));

        const int dimBase = pathIntegratorDimBaseForBounce(depth);
        const Vec3 direct = pathIntegratorSampleDirectLighting(
            position,
            normal,
            wo,
            material,
            scene,
            params,
            env,
            ctx,
            dimBase,
            sobolMatrices,
            sobolDimensionCount);
        radiance = vecAdd3(radiance, vecMake3(
            throughput.x * direct.x,
            throughput.y * direct.y,
            throughput.z * direct.z));

        const float fresnel = bsdfFresnelDielectric(
            vecDot3(normal, wo),
            insideMedium ? material.ior : 1.0f,
            insideMedium ? 1.0f : material.ior);
        const float transmissionWeight = material.transmission * (1.0f - fresnel);
        const float reflectionWeight = 1.0f - transmissionWeight;
        const float lobeChoice = pathIntegratorNext1D(ctx, dimBase + 8, sobolMatrices, sobolDimensionCount);

        BsdfSampleResult sample{};
        if (transmissionWeight > 0.0f && lobeChoice < transmissionWeight) {
            const float eta = insideMedium ? (material.ior / 1.0f) : (1.0f / material.ior);
            const float u1 = pathIntegratorNext1D(ctx, dimBase + 9, sobolMatrices, sobolDimensionCount);
            const float u2 = pathIntegratorNext1D(ctx, dimBase + 10, sobolMatrices, sobolDimensionCount);
            sample = bsdfSampleTransmission(normal, wo, material, eta, u1, u2);
            if (sample.valid) {
                insideMedium = !insideMedium;
            }
        } else {
            const float uLobe = pathIntegratorNext1D(ctx, dimBase + 9, sobolMatrices, sobolDimensionCount);
            const float u1 = pathIntegratorNext1D(ctx, dimBase + 10, sobolMatrices, sobolDimensionCount);
            const float u2 = pathIntegratorNext1D(ctx, dimBase + 11, sobolMatrices, sobolDimensionCount);
            sample = bsdfSample(normal, wo, material, uLobe * reflectionWeight, u1, u2);
        }

        if (!sample.valid || sample.pdf <= PathIntegratorDetail::kMinPdf) {
            break;
        }

        const Vec3 bsdfValue = bsdfEval(normal, wo, sample.direction, material);
        const float cosTheta = vecMax2(0.0f, vecDot3(normal, sample.direction));
        throughput = vecMake3(
            throughput.x * bsdfValue.x * cosTheta / sample.pdf,
            throughput.y * bsdfValue.y * cosTheta / sample.pdf,
            throughput.z * bsdfValue.z * cosTheta / sample.pdf);

        if (depth >= PathIntegratorDetail::kRussianRouletteStartDepth) {
            const float survivalProb = vecMin2(
                PathIntegratorDetail::kMaxRussianRouletteProb,
                vecMax2(bsdfLuminance(throughput), 0.05f));
            const float rr = pathIntegratorNext1D(ctx, dimBase + 11, sobolMatrices, sobolDimensionCount);
            if (rr > survivalProb) {
                break;
            }
            throughput = vecScale3(throughput, 1.0f / survivalProb);
        }

        const Vec3 orientedNormal = sample.lobe == BsdfLobe::Transmit ? vecScale3(normal, -1.0f) : normal;
        currentOrigin = vecAdd3(position, vecScale3(orientedNormal, PathIntegratorDetail::kShadowBias));
        currentDir = sample.direction;
        currentHit = meshAccelTraceRay(
            currentOrigin,
            currentDir,
            scene,
            PathIntegratorDetail::kShadowBias,
            PathIntegratorDetail::kRayTMax);
        if (!currentHit.hit) {
            const Vec3 envRadiance = lightEvalEnvironmentOrBackground(env, params, currentDir);
            radiance = vecAdd3(radiance, vecMake3(
                throughput.x * envRadiance.x,
                throughput.y * envRadiance.y,
                throughput.z * envRadiance.z));
            break;
        }
    }

    return radiance;
}

PATH_INTEGRATOR_FN Vec3 tracePath(
    Vec3 rayOrigin,
    Vec3 rayDir,
    const MeshAccelSceneGpu* scene,
    const RenderParamsGpu* params,
    const EnvironmentMapGpu* env,
    SampleContext& ctx,
    const uint32_t* sobolMatrices,
    int sobolDimensionCount)
{
    const MeshHit hit = meshAccelTraceRay(
        rayOrigin, rayDir, scene, 0.0f, PathIntegratorDetail::kRayTMax);
    return tracePathFromHit(
        hit, rayOrigin, rayDir, scene, params, env, ctx, sobolMatrices, sobolDimensionCount);
}

#undef PATH_INTEGRATOR_FN
