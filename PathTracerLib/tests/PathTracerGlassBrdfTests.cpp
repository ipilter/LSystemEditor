#include "Brdf/BrdfDispatch.h"
#include "Brdf/BrdfBase.h"
#include "Brdf/GlassBrdf.h"
#include "MeshAccel/MeshAccelTypes.h"

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

MaterialGpu makeMat3Glass()
{
    MaterialGpu material{};
    material.r = 0.95f;
    material.g = 0.98f;
    material.b = 1.00f;
    material.roughness = 0.01f;
    material.ior = 1.52f;
    material.transmission = 1.0f;
    material.kind = 2;
    return material;
}

void testBrdfForMaterial_GlassKind()
{
    const MaterialGpu material = makeMat3Glass();
    expectTrue(brdfForMaterial(material) == BrdfType::Glass, "glass kind dispatches to Glass BRDF");
}

void testGlassEval_TransmittedDirection_NonZero()
{
    const MaterialGpu material = makeMat3Glass();
    const Vec3 normal = vecMake3(0.0f, 1.0f, 0.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.1f, 0.9f, 0.2f));

    Vec3 refracted{};
    expectTrue(brdfRefract3(wo, normal, material.ior, refracted), "refract succeeds");

    const BrdfContext ctx{normal, wo, material};
    const GlassBrdf brdf{};
    const Vec3 value = brdf.eval(ctx, refracted);
    expectTrue(value.x > 0.0f, "transmitted eval r > 0");
    expectTrue(value.y > 0.0f, "transmitted eval g > 0");
    expectTrue(value.z > 0.0f, "transmitted eval b > 0");
}

void testGlassSample_Transmitted_ThroughputNonZero()
{
    const MaterialGpu material = makeMat3Glass();
    const Vec3 normal = vecMake3(0.0f, 1.0f, 0.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.0f, 1.0f, 0.0f));

    const BrdfContext ctx{normal, wo, material};
    const GlassBrdf brdf{};
    const BrdfSampleResult sample = brdf.sample(ctx, 0.99f, 0.5f);
    expectTrue(sample.valid, "transmitted sample valid");
    expectTrue(sample.transmitted, "sample marked transmitted");

    const Vec3 bsdfValue = brdf.eval(ctx, sample.direction);
    const float cosTheta = std::fabs(vecDot3(normal, sample.direction));
    const float throughputScale = bsdfValue.x * cosTheta / sample.pdf;
    expectNear(throughputScale, material.r, 0.05f, "transmitted throughput scale near base color");
}

void testGlassEval_ReflectionDirection_NonZero()
{
    MaterialGpu material = makeMat3Glass();
    material.roughness = 0.5f;
    const Vec3 normal = vecMake3(0.0f, 1.0f, 0.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.3f, 0.9f, 0.2f));
    const Vec3 wi = vecNormalize3(vecMake3(0.25f, 0.95f, 0.15f));
    const BrdfContext ctx{normal, wo, material};

    expectTrue(vecDot3(normal, wi) > 0.0f, "wi in upper hemisphere");

    const GlassBrdf glass{};
    const Vec3 value = glass.eval(ctx, wi);
    expectTrue(value.x > 0.0f, "reflection eval r > 0");
}

void testGlassFresnel_AtNormalIncidence()
{
    const float fresnel = brdfDielectricFresnel(1.0f, 1.0f, 1.52f);
    expectTrue(fresnel > 0.03f && fresnel < 0.06f, "fresnel at normal incidence");
}

void testGlassFresnel_AtGrazing()
{
    const float fresnel = brdfDielectricFresnel(0.05f, 1.0f, 1.52f);
    expectTrue(fresnel > 0.5f, "fresnel high at grazing angle");
}

void testGlassSample_GrazingReflection_Valid()
{
    const MaterialGpu material = makeMat3Glass();
    const Vec3 normal = vecMake3(0.0f, 1.0f, 0.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.995f, 0.1f, 0.0f));
    const BrdfContext ctx{normal, wo, material};
    const GlassBrdf brdf{};
    const BrdfSampleResult sample = brdf.sample(ctx, 0.0f, 0.5f);
    expectTrue(sample.valid, "grazing reflection sample valid");
    expectTrue(!sample.transmitted, "grazing sample is reflection");
}

void testGlassNee_ReflectionWeight_NonZeroAtGrazing()
{
    const MaterialGpu material = makeMat3Glass();
    const Vec3 normal = vecMake3(0.0f, 1.0f, 0.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.995f, 0.1f, 0.0f));
    const BrdfContext ctx{normal, wo, material};
    const GlassBrdf brdf{};
    const float cosWo = vecMax2(0.0f, vecDot3(normal, wo));
    const float fresnel = brdfDielectricFresnel(cosWo, 1.0f, material.ior);
    const BrdfSampleResult spec = brdf.sampleSpecularReflection(ctx, fresnel);
    expectTrue(spec.valid, "specular fallback valid for nee path");
    const Vec3 bsdf = brdf.eval(ctx, spec.direction);
    const float cosTheta = vecMax2(0.0f, vecDot3(normal, spec.direction));
    expectTrue(bsdf.x * cosTheta > 0.0f, "glass nee reflection weight non-zero");
}

void testGlassEval_SpecularReflection_MatchesSamplePdf()
{
    const MaterialGpu material = makeMat3Glass();
    const Vec3 normal = vecMake3(0.0f, 1.0f, 0.0f);
    const Vec3 wo = vecNormalize3(vecMake3(0.995f, 0.1f, 0.0f));
    const BrdfContext ctx{normal, wo, material};
    const GlassBrdf brdf{};
    const float cosWo = vecMax2(0.0f, vecDot3(normal, wo));
    const float fresnel = brdfDielectricFresnel(cosWo, 1.0f, material.ior);
    const BrdfSampleResult sample = brdf.sampleSpecularReflection(ctx, fresnel);
    expectTrue(sample.valid, "specular sample valid");
    const Vec3 eval = brdf.eval(ctx, sample.direction);
    const float cosWi = vecMax2(0.0f, vecDot3(normal, sample.direction));
    const float throughput = eval.x * cosWi / sample.pdf;
    expectTrue(throughput > 0.95f && throughput < 1.05f, "specular throughput matches baseColor");
}

} // namespace

int main()
{
    testBrdfForMaterial_GlassKind();
    testGlassEval_TransmittedDirection_NonZero();
    testGlassSample_Transmitted_ThroughputNonZero();
    testGlassEval_ReflectionDirection_NonZero();
    testGlassFresnel_AtNormalIncidence();
    testGlassFresnel_AtGrazing();
    testGlassSample_GrazingReflection_Valid();
    testGlassNee_ReflectionWeight_NonZeroAtGrazing();
    testGlassEval_SpecularReflection_MatchesSamplePdf();

    if (gFailures != 0) {
        std::cerr << gFailures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All glass BRDF tests passed.\n";
    return 0;
}
