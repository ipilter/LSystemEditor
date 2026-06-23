#include "Texture/ProceduralTexture.h"

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

TextureDescGpu makeGridTexture(
    float onR,
    float onG,
    float onB,
    float offR,
    float offG,
    float offB,
    float frequency,
    float thickness,
    float intensityOn = 1.0f,
    float intensityOff = 1.0f)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Grid2D);
    desc.p0 = make_float4(frequency, frequency, thickness, 0.0f);
    desc.p1 = make_float4(onR, onG, onB, intensityOn);
    desc.p2 = make_float4(offR, offG, offB, intensityOff);
    return desc;
}

TextureDescGpu makeStripeTexture(
    float frequency,
    float thickness,
    float onValue,
    float offValue,
    float intensityOn = 1.0f,
    float intensityOff = 1.0f)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Stripe1D);
    desc.p0 = make_float4(frequency, thickness, 0.0f, 0.0f);
    desc.p1 = make_float4(onValue, onValue, onValue, intensityOn);
    desc.p2 = make_float4(offValue, offValue, offValue, intensityOff);
    return desc;
}

TextureDescGpu makeNoiseTexture(
    float onValue,
    float offValue,
    float scale,
    float octaves = 1.0f,
    float seed = 0.0f,
    float intensityOn = 1.0f,
    float intensityOff = 1.0f)
{
    TextureDescGpu desc{};
    desc.kind = static_cast<uint32_t>(TextureKind::Noise2D);
    desc.p0 = make_float4(scale, octaves, seed, 0.0f);
    desc.p1 = make_float4(onValue, onValue, onValue, intensityOn);
    desc.p2 = make_float4(offValue, offValue, offValue, intensityOff);
    return desc;
}

void testGridReturnsOnColorOffGridLines()
{
    const TextureDescGpu grid = makeGridTexture(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.05f);
    const TextureEvalContext ctx{Vec2{0.3f, 0.3f}, 0.3f};
    const Vec3 color = evalProceduralRgb(grid, ctx);
    expectNear(color.x, 1.0f, 1e-5f, "grid on color red off lines");
    expectNear(color.y, 0.0f, 1e-5f, "grid on color green off lines");
    expectNear(color.z, 0.0f, 1e-5f, "grid on color blue off lines");
}

void testGridReturnsOffColorOnGridLines()
{
    const TextureDescGpu grid = makeGridTexture(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.05f);
    const TextureEvalContext ctx{Vec2{0.0f, 0.0f}, 0.0f};
    const Vec3 color = evalProceduralRgb(grid, ctx);
    expectNear(color.x, 0.0f, 1e-5f, "grid off color red on lines");
    expectNear(color.y, 1.0f, 1e-5f, "grid off color green on lines");
    expectNear(color.z, 0.0f, 1e-5f, "grid off color blue on lines");
}

void testGridSplitFrequencyUsesSeparateAxes()
{
    TextureDescGpu splitGrid{};
    splitGrid.kind = static_cast<uint32_t>(TextureKind::Grid2D);
    splitGrid.p0 = make_float4(20.0f, 2.0f, 0.05f, 0.0f);
    splitGrid.p1 = make_float4(1.0f, 0.0f, 0.0f, 1.0f);
    splitGrid.p2 = make_float4(0.0f, 1.0f, 0.0f, 1.0f);

    const TextureDescGpu uniformGrid = makeGridTexture(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 20.0f, 0.05f);
    const TextureEvalContext ctx{Vec2{0.03f, 0.02f}, 0.0f};

    const Vec3 splitColor = evalProceduralRgb(splitGrid, ctx);
    const Vec3 uniformColor = evalProceduralRgb(uniformGrid, ctx);
    expectNear(splitColor.y, 1.0f, 1e-5f, "split grid hits low-frequency V line");
    expectNear(uniformColor.x, 1.0f, 1e-5f, "uniform grid misses both axes at same UV");
}

void testStripeAlternatesScalarValues()
{
    const TextureDescGpu stripe = makeStripeTexture(2.0f, 0.5f, 0.25f, 1.0f);
    const TextureEvalContext onCtx{Vec2{0.0f, 0.0f}, 0.0f};
    const TextureEvalContext offCtx{Vec2{0.0f, 0.0f}, 0.4f};
    expectNear(evalProceduralScalar(stripe, onCtx), 0.25f, 1e-5f, "stripe on value");
    expectNear(evalProceduralScalar(stripe, offCtx), 1.0f, 1e-5f, "stripe off value");
}

void testResolveMaterialUsesInlineWithoutTextureIndex()
{
    MaterialGpu material{};
    material.r = 0.2f;
    material.g = 0.3f;
    material.b = 0.4f;
    material.roughness = 0.7f;

    const TextureEvalContext ctx{Vec2{0.25f, 0.75f}, 0.25f};
    const ResolvedMaterial resolved = resolveMaterial(material, ctx, nullptr, 0u);
    expectNear(resolved.r, 0.2f, 1e-5f, "inline albedo r");
    expectNear(resolved.g, 0.3f, 1e-5f, "inline albedo g");
    expectNear(resolved.b, 0.4f, 1e-5f, "inline albedo b");
    expectNear(resolved.roughness, 0.7f, 1e-5f, "inline roughness");
}

void testResolveMaterialAppliesGridAlbedoTexture()
{
    MaterialGpu material{};
    material.albedoTex = 1u;

    const TextureDescGpu bank[] = {
        {},
        makeGridTexture(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.05f),
    };

    const TextureEvalContext ctx{Vec2{0.0f, 0.0f}, 0.0f};
    const ResolvedMaterial resolved = resolveMaterial(material, ctx, bank, 2u);
    expectNear(resolved.r, 0.0f, 1e-5f, "resolved grid albedo r");
    expectNear(resolved.g, 1.0f, 1e-5f, "resolved grid albedo g");
    expectNear(resolved.b, 0.0f, 1e-5f, "resolved grid albedo b");
}

void testGridScalarUsesOnOffValues()
{
    const TextureDescGpu grid = makeGridTexture(2.0f, 2.0f, 2.0f, 0.5f, 0.5f, 0.5f, 4.0f, 0.05f);

    const TextureEvalContext onLine{Vec2{0.0f, 0.0f}, 0.0f};
    const TextureEvalContext offLine{Vec2{0.3f, 0.3f}, 0.3f};
    expectNear(evalProceduralScalar(grid, onLine), 0.5f, 1e-5f, "grid scalar off value on line");
    expectNear(evalProceduralScalar(grid, offLine), 2.0f, 1e-5f, "grid scalar on value off line");
}

void testMultiplyEmissionWithGridMask()
{
    MaterialGpu material{};
    material.emission = 5.0f;
    material.emissionTex = 1u;

    const TextureDescGpu bank[] = {
        {},
        makeGridTexture(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 4.0f, 0.05f),
    };

    const TextureEvalContext onLine{Vec2{0.0f, 0.0f}, 0.0f};
    const TextureEvalContext offLine{Vec2{0.3f, 0.3f}, 0.3f};
    const ResolvedMaterial onResolved = resolveMaterial(material, onLine, bank, 2u);
    const ResolvedMaterial offResolved = resolveMaterial(material, offLine, bank, 2u);
    expectNear(onResolved.emission, 5.0f, 1e-5f, "grid emission on line");
    expectNear(offResolved.emission, 0.0f, 1e-5f, "grid emission off line");
}

void testInlineZeroEmissionUsesStripeReplace()
{
    MaterialGpu material{};
    material.emission = 0.0f;
    material.emissionTex = 1u;

    const TextureDescGpu bank[] = {
        {},
        makeStripeTexture(2.0f, 0.5f, 10.0f, 0.0f),
    };

    const TextureEvalContext onCtx{Vec2{0.0f, 0.0f}, 0.0f};
    const TextureEvalContext offCtx{Vec2{0.0f, 0.0f}, 0.4f};
    const ResolvedMaterial onResolved = resolveMaterial(material, onCtx, bank, 2u);
    const ResolvedMaterial offResolved = resolveMaterial(material, offCtx, bank, 2u);
    expectNear(onResolved.emission, 10.0f, 1e-5f, "stripe emission replace on");
    expectNear(offResolved.emission, 0.0f, 1e-5f, "stripe emission replace off");
}

void testNoiseRoughnessMultiply()
{
    MaterialGpu material{};
    material.roughness = 1.0f;
    material.roughnessTex = 1u;

    const TextureDescGpu noise = makeNoiseTexture(0.8f, 0.2f, 4.0f, 1.0f, 42.0f);
    const TextureDescGpu bank[] = {{}, noise};
    const TextureEvalContext ctx{Vec2{0.25f, 0.75f}, 0.25f};
    const ResolvedMaterial resolved = resolveMaterial(material, ctx, bank, 2u);
    expectTrue(resolved.roughness >= 0.2f - 1e-5f, "noise roughness min bound");
    expectTrue(resolved.roughness <= 0.8f + 1e-5f, "noise roughness max bound");
}

void testNoiseAlbedoRgbIsNonZero()
{
    const TextureDescGpu noise = makeNoiseTexture(1.0f, 0.0f, 16.0f, 4.0f, 0.0f);
    const TextureEvalContext ctx{Vec2{0.25f, 0.75f}, 0.25f};
    const Vec3 color = evalProceduralRgb(noise, ctx);
    expectTrue(color.x > 0.0f || color.y > 0.0f || color.z > 0.0f, "noise albedo rgb non-zero");
}

void testNoiseEmissionIntensityBoost()
{
    MaterialGpu material{};
    material.emission = 0.0f;
    material.emissionTex = 1u;

    const TextureDescGpu noise = makeNoiseTexture(1.0f, 0.0f, 16.0f, 4.0f, 0.0f, 50.0f, 0.0f);
    const TextureDescGpu bank[] = {{}, noise};
    const TextureEvalContext ctx{Vec2{0.25f, 0.75f}, 0.25f};
    const ResolvedMaterial resolved = resolveMaterial(material, ctx, bank, 2u);
    expectTrue(resolved.emission > 1.0f, "noise emission intensity boost");
}

void testEstimateEmissiveLuminanceFromStripeTexture()
{
    MaterialGpu material{};
    material.r = 0.9f;
    material.g = 0.8f;
    material.b = 0.7f;
    material.emission = 0.0f;
    material.emissionTex = 1u;

    const TextureDescGpu stripe = makeStripeTexture(20.0f, 0.25f, 1.0f, 0.0f, 50.0f, 0.0f);
    const TextureDescGpu bank[] = {{}, stripe};
    const float luminance = estimateMaterialEmissionLuminance(material, bank, 2u);
    expectTrue(luminance > 0.0f, "stripe emission texture contributes to light sampling estimate");
}

} // namespace

int main()
{
    testGridReturnsOnColorOffGridLines();
    testGridReturnsOffColorOnGridLines();
    testGridSplitFrequencyUsesSeparateAxes();
    testStripeAlternatesScalarValues();
    testResolveMaterialUsesInlineWithoutTextureIndex();
    testResolveMaterialAppliesGridAlbedoTexture();
    testGridScalarUsesOnOffValues();
    testMultiplyEmissionWithGridMask();
    testInlineZeroEmissionUsesStripeReplace();
    testNoiseRoughnessMultiply();
    testNoiseAlbedoRgbIsNonZero();
    testNoiseEmissionIntensityBoost();
    testEstimateEmissiveLuminanceFromStripeTexture();

    if (gFailures == 0) {
        std::cout << "All procedural texture tests passed.\n";
        return 0;
    }

    std::cerr << gFailures << " procedural texture test(s) failed.\n";
    return 1;
}
