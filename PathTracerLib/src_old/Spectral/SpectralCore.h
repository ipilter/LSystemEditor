#pragma once

#include "Geometry/MathCore.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Rgb2Spec.h"

#include <cmath>

#if defined(__CUDACC__)
#include "SpectralDevice.cuh"
#include "SpectralState.h"
#else
#include "SpectralState.h"
#endif

#if defined(__CUDACC__)
#define SPECTRAL_CORE_FN __host__ __device__ inline
#else
#define SPECTRAL_CORE_FN inline
#endif

SPECTRAL_CORE_FN Rgb2SpecGpu spectralCurrentModel()
{
#if defined(__CUDA_ARCH__)
    return spectralActiveModel();
#else
    return spectralHostModel();
#endif
}

namespace SpectralDetail {

constexpr float kLambdaMin = 360.0f;
constexpr float kLambdaMax = 830.0f;
constexpr float kLambdaRange = kLambdaMax - kLambdaMin;
constexpr float kReferenceWavelengthNm = 589.3f;
constexpr float kMinAbbe = 1.0f;
constexpr int kCieSamples = 95;
constexpr float kCieLambdaStep = 5.0f;

// CIE 1931 2-degree CMFs and D65 (unit luminance) from rgb2spec/details/cie1931.h.
SPECTRAL_CORE_FN float cieTableInterp(const float* data, float lambdaNm)
{
    float x = lambdaNm - kLambdaMin;
    x *= static_cast<float>(kCieSamples - 1) / kLambdaRange;
    const int offset = vecClamp(static_cast<int>(x), 0, kCieSamples - 2);
    const float weight = x - static_cast<float>(offset);
    return data[offset] * (1.0f - weight) + data[offset + 1] * weight;
}

SPECTRAL_CORE_FN float cieXAtWavelength(float lambdaNm)
{
    static const float kCieX[kCieSamples] = {
        0.0001299f, 0.0002321f, 0.0004149f, 0.0007416f, 0.0013680f, 0.0022360f, 0.0042430f, 0.0076500f,
        0.0143100f, 0.0231900f, 0.0435100f, 0.0776300f, 0.1343800f, 0.2147700f, 0.2839000f, 0.3285000f,
        0.3482800f, 0.3480600f, 0.3362000f, 0.3187000f, 0.2908000f, 0.2511000f, 0.1953600f, 0.1421000f,
        0.0956400f, 0.0579500f, 0.0320100f, 0.0147000f, 0.0049000f, 0.0024000f, 0.0093000f, 0.0291000f,
        0.0632700f, 0.1096000f, 0.1655000f, 0.2257499f, 0.2904000f, 0.3597000f, 0.4334499f, 0.5120501f,
        0.5945000f, 0.6784000f, 0.7621000f, 0.8425000f, 0.9163000f, 0.9786000f, 1.0263000f, 1.0567000f,
        1.0622000f, 1.0456000f, 1.0026000f, 0.9384000f, 0.8544499f, 0.7514000f, 0.6424000f, 0.5419000f,
        0.4479000f, 0.3608000f, 0.2835000f, 0.2187000f, 0.1649000f, 0.1212000f, 0.0874000f, 0.0636000f,
        0.0467700f, 0.0329000f, 0.0227000f, 0.0158400f, 0.0113592f, 0.0081109f, 0.0057903f, 0.0041095f,
        0.0028993f, 0.0020492f, 0.0014400f, 0.0009999f, 0.0006901f, 0.0004760f, 0.0003323f, 0.0002348f,
        0.0001662f, 0.0001174f, 0.0000831f, 0.0000587f, 0.0000415f, 0.0000294f, 0.0000207f, 0.0000146f,
        0.0000103f, 0.0000072f, 0.0000051f, 0.0000036f, 0.0000025f, 0.0000018f, 0.0000013f};
    return cieTableInterp(kCieX, lambdaNm);
}

SPECTRAL_CORE_FN float cieYAtWavelength(float lambdaNm)
{
    static const float kCieY[kCieSamples] = {
        0.0000039f, 0.0000070f, 0.0000124f, 0.0000220f, 0.0000390f, 0.0000640f, 0.0001200f, 0.0002170f,
        0.0003960f, 0.0006400f, 0.0012100f, 0.0021800f, 0.0040000f, 0.0073000f, 0.0116000f, 0.0168400f,
        0.0230000f, 0.0298000f, 0.0380000f, 0.0480000f, 0.0600000f, 0.0739000f, 0.0909800f, 0.1126000f,
        0.1390200f, 0.1693000f, 0.2080200f, 0.2586000f, 0.3230000f, 0.4073000f, 0.5030000f, 0.6082000f,
        0.7100000f, 0.7932000f, 0.8620000f, 0.9148501f, 0.9540000f, 0.9803000f, 0.9949501f, 1.0000000f,
        0.9950000f, 0.9786000f, 0.9520000f, 0.9154000f, 0.8700000f, 0.8163000f, 0.7570000f, 0.6949000f,
        0.6310000f, 0.5668000f, 0.5030000f, 0.4412000f, 0.3810000f, 0.3210000f, 0.2650000f, 0.2170000f,
        0.1750000f, 0.1382000f, 0.1070000f, 0.0816000f, 0.0610000f, 0.0445800f, 0.0320000f, 0.0232000f,
        0.0170000f, 0.0119200f, 0.0082100f, 0.0057230f, 0.0041020f, 0.0029290f, 0.0020910f, 0.0014840f,
        0.0010470f, 0.0007400f, 0.0005200f, 0.0003611f, 0.0002492f, 0.0001719f, 0.0001200f, 0.0000848f,
        0.0000600f, 0.0000424f, 0.0000300f, 0.0000212f, 0.0000150f, 0.0000106f, 0.0000075f, 0.0000053f,
        0.0000037f, 0.0000026f, 0.0000018f, 0.0000013f, 0.0000009f, 0.0000006f, 0.0000005f};
    return cieTableInterp(kCieY, lambdaNm);
}

SPECTRAL_CORE_FN float cieZAtWavelength(float lambdaNm)
{
    static const float kCieZ[kCieSamples] = {
        0.0006061f, 0.0010860f, 0.0019460f, 0.0034860f, 0.0064500f, 0.0105500f, 0.0200500f, 0.0362100f,
        0.0678500f, 0.1102000f, 0.2074000f, 0.3713000f, 0.6456000f, 1.0390501f, 1.3856000f, 1.6229600f,
        1.7470600f, 1.7826000f, 1.7721100f, 1.7441000f, 1.6692000f, 1.5281000f, 1.2876400f, 1.0419000f,
        0.8129501f, 0.6162000f, 0.4651800f, 0.3533000f, 0.2720000f, 0.2123000f, 0.1582000f, 0.1117000f,
        0.0782500f, 0.0572500f, 0.0421600f, 0.0298400f, 0.0203000f, 0.0134000f, 0.0087500f, 0.0057500f,
        0.0039000f, 0.0027500f, 0.0021000f, 0.0018000f, 0.0016500f, 0.0014000f, 0.0011000f, 0.0010000f,
        0.0008000f, 0.0006000f, 0.0003400f, 0.0002400f, 0.0001900f, 0.0001000f, 0.0000500f, 0.0000300f,
        0.0000200f, 0.0000100f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f,
        0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f,
        0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f,
        0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f,
        0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f};
    return cieTableInterp(kCieZ, lambdaNm);
}

SPECTRAL_CORE_FN float d65IlluminantAtWavelength(float lambdaNm)
{
    static const float kD65[kCieSamples] = {
        0.0044149f, 0.0046696f, 0.0049295f, 0.0048296f, 0.0047297f, 0.0049505f, 0.0051713f, 0.0065029f,
        0.0078345f, 0.0082536f, 0.0086727f, 0.0087648f, 0.0088569f, 0.0085236f, 0.0081903f, 0.0090647f,
        0.0099391f, 0.0105176f, 0.0110961f, 0.0111342f, 0.0111723f, 0.0110149f, 0.0108575f, 0.0109156f,
        0.0109737f, 0.0106372f, 0.0103007f, 0.0103264f, 0.0103521f, 0.0102767f, 0.0102013f, 0.0100585f,
        0.0099157f, 0.0100529f, 0.0101902f, 0.0100350f, 0.0098798f, 0.0098614f, 0.0098430f, 0.0096536f,
        0.0094642f, 0.0092965f, 0.0091288f, 0.0089158f, 0.0087028f, 0.0085343f, 0.0083658f, 0.0084788f,
        0.0085918f, 0.0085021f, 0.0084124f, 0.0083318f, 0.0083010f, 0.0080875f, 0.0078740f, 0.0079258f,
        0.0079776f, 0.0077486f, 0.0075196f, 0.0075888f, 0.0076580f, 0.0076876f, 0.0077863f, 0.0077097f,
        0.0076331f, 0.0070075f, 0.0063819f, 0.0064763f, 0.0065707f, 0.0066978f, 0.0068249f, 0.0064368f,
        0.0060487f, 0.0062241f, 0.0063995f, 0.0066248f, 0.0068490f, 0.0063415f, 0.0058340f, 0.0052103f,
        0.0045866f, 0.0055938f, 0.0066010f, 0.0064092f, 0.0062174f, 0.0062443f, 0.0062712f, 0.0060070f,
        0.0057428f, 0.0053531f, 0.0049634f, 0.0051818f, 0.0054002f, 0.0055365f, 0.0056728f};
    return cieTableInterp(kD65, lambdaNm);
}

SPECTRAL_CORE_FN void spectralLinearSrgbFromXyz(float x, float y, float z, float& r, float& g, float& b)
{
    r = 3.240479f * x - 1.537150f * y - 0.498535f * z;
    g = -0.969256f * x + 1.875991f * y + 0.041556f * z;
    b = 0.055648f * x - 0.204043f * y + 1.057311f * z;
}

SPECTRAL_CORE_FN void spectralRgbResponseAtWavelength(
    float lambdaNm,
    float& responseR,
    float& responseG,
    float& responseB)
{
    const float xBar = cieXAtWavelength(lambdaNm);
    const float yBar = cieYAtWavelength(lambdaNm);
    const float zBar = cieZAtWavelength(lambdaNm);
    const float illuminant = d65IlluminantAtWavelength(lambdaNm);

    const float x = xBar * illuminant;
    const float y = yBar * illuminant;
    const float z = zBar * illuminant;
    spectralLinearSrgbFromXyz(x, y, z, responseR, responseG, responseB);
}

} // namespace SpectralDetail

SPECTRAL_CORE_FN float spectralClampLambda(float lambdaNm)
{
    return vecMax2(SpectralDetail::kLambdaMin, vecMin2(SpectralDetail::kLambdaMax, lambdaNm));
}

SPECTRAL_CORE_FN void spectralCmfAtWavelength(float lambdaNm, float& xBar, float& yBar, float& zBar)
{
    const float clamped = spectralClampLambda(lambdaNm);
    xBar = SpectralDetail::cieXAtWavelength(clamped);
    yBar = SpectralDetail::cieYAtWavelength(clamped);
    zBar = SpectralDetail::cieZAtWavelength(clamped);
}

SPECTRAL_CORE_FN void spectralSampleWavelength(float u, float& lambdaNm, float& pdf)
{
    lambdaNm = SpectralDetail::kLambdaMin + u * SpectralDetail::kLambdaRange;
    pdf = 1.0f / SpectralDetail::kLambdaRange;
}

SPECTRAL_CORE_FN float spectralIorAtWavelength(float iorRef, float abbe, float lambdaNm)
{
    const float abbeClamped = vecMax2(abbe, SpectralDetail::kMinAbbe);
    const float lambdaUm = spectralClampLambda(lambdaNm) * 1.0e-3f;
    const float lambdaRefUm = SpectralDetail::kReferenceWavelengthNm * 1.0e-3f;
    const float b = (iorRef - 1.0f) / abbeClamped;
    const float invLambda2 = 1.0f / vecMax2(lambdaUm * lambdaUm, 1.0e-12f);
    const float invRef2 = 1.0f / vecMax2(lambdaRefUm * lambdaRefUm, 1.0e-12f);
    return iorRef + b * (invLambda2 - invRef2);
}

SPECTRAL_CORE_FN float spectralRgbToScalar(float r, float g, float b, float lambdaNm)
{
    const Rgb2SpecGpu model = spectralCurrentModel();
    return rgb2specEvalReflectance(model, r, g, b, lambdaNm);
}

SPECTRAL_CORE_FN Vec3 spectralToRgb(float spectralRadiance, float lambdaNm, float wavelengthPdf)
{
    if (!isfinite(spectralRadiance) || spectralRadiance <= 0.0f) {
        return vecMake3(0.0f, 0.0f, 0.0f);
    }

    const Rgb2SpecGpu model = spectralCurrentModel();
    const float scale = spectralRadiance / vecMax2(wavelengthPdf, 1.0e-8f);

    float responseR = 0.0f;
    float responseG = 0.0f;
    float responseB = 0.0f;
    SpectralDetail::spectralRgbResponseAtWavelength(lambdaNm, responseR, responseG, responseB);

    return vecMake3(
        scale * responseR / vecMax2(model.whiteNormR, 1.0e-8f),
        scale * responseG / vecMax2(model.whiteNormG, 1.0e-8f),
        scale * responseB / vecMax2(model.whiteNormB, 1.0e-8f));
}

SPECTRAL_CORE_FN float spectralRgbReflectanceAtWavelength(float r, float g, float b, float lambdaNm)
{
    const Rgb2SpecGpu model = spectralCurrentModel();
    return rgb2specEvalReflectance(model, r, g, b, lambdaNm);
}

SPECTRAL_CORE_FN float spectralGlassAbsorptionAtWavelength(
    float sigmaR,
    float sigmaG,
    float sigmaB,
    float lambdaNm)
{
    const Rgb2SpecGpu model = spectralCurrentModel();
    return rgb2specEvalReflectance(model, sigmaR, sigmaG, sigmaB, lambdaNm);
}

SPECTRAL_CORE_FN float spectralGlassIor(const MaterialGpu& material, float lambdaNm)
{
    return spectralIorAtWavelength(
        vecMax2(material.ior, 1.0e-3f),
        material.abbeNumber,
        lambdaNm);
}

SPECTRAL_CORE_FN float spectralEnvironmentRadianceAtWavelength(Vec3 rgb, float lambdaNm)
{
    const Rgb2SpecGpu model = spectralCurrentModel();
    return rgb2specEvalRadiance(model, rgb.x, rgb.y, rgb.z, lambdaNm);
}


#undef SPECTRAL_CORE_FN
