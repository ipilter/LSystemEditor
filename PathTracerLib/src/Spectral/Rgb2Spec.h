#pragma once

#include "Geometry/MathCore.h"

#include <cmath>
#include <cstdint>

#if defined(__CUDACC__)
#define RGB2SPEC_FN __host__ __device__ inline
#else
#define RGB2SPEC_FN inline
#endif

namespace Rgb2SpecDetail {

constexpr int kCoeffCount = 3;

RGB2SPEC_FN float clamp01(float value)
{
    return vecMax2(0.0f, vecMin2(1.0f, value));
}

RGB2SPEC_FN int findInterval(const float* scale, int res, float x)
{
    int left = 0;
    const int lastInterval = res - 2;
    int size = lastInterval;
    while (size > 0) {
        const int half = size >> 1;
        const int middle = left + half + 1;
        if (scale[middle] <= x) {
            left = middle;
            size -= half + 1;
        } else {
            size = half;
        }
    }
    return vecMin2(left, lastInterval);
}

} // namespace Rgb2SpecDetail

struct Rgb2SpecGpu
{
    const float* scale = nullptr;
    const float* data = nullptr;
    int res = 0;
    float whiteNormR = 1.0f;
    float whiteNormG = 1.0f;
    float whiteNormB = 1.0f;
};

RGB2SPEC_FN float rgb2specEvalCoefficients(
    const float coeff[Rgb2SpecDetail::kCoeffCount],
    float lambdaNm)
{
    const float lambda = lambdaNm;
    const float x = coeff[0] * lambda * lambda + coeff[1] * lambda + coeff[2];
    const float y = 1.0f / sqrtf(x * x + 1.0f);
    return 0.5f * x * y + 0.5f;
}

RGB2SPEC_FN void rgb2specFetchCoefficients(
    const Rgb2SpecGpu& model,
    float rgb[3],
    float out[Rgb2SpecDetail::kCoeffCount])
{
    if (model.scale == nullptr || model.data == nullptr || model.res <= 1) {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        return;
    }

    const int res = model.res;
    float clampedRgb[3] = {
        Rgb2SpecDetail::clamp01(rgb[0]),
        Rgb2SpecDetail::clamp01(rgb[1]),
        Rgb2SpecDetail::clamp01(rgb[2])};

    int dominant = 0;
    for (int channel = 1; channel < 3; ++channel) {
        if (clampedRgb[channel] >= clampedRgb[dominant]) {
            dominant = channel;
        }
    }

    const float z = clampedRgb[dominant];
    if (z <= 1.0e-8f) {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        return;
    }

    const float scaleFactor = static_cast<float>(res - 1) / z;
    const float x = clampedRgb[(dominant + 1) % 3] * scaleFactor;
    const float y = clampedRgb[(dominant + 2) % 3] * scaleFactor;

    const uint32_t xi = static_cast<uint32_t>(vecMin2(x, static_cast<float>(res - 2)));
    const uint32_t yi = static_cast<uint32_t>(vecMin2(y, static_cast<float>(res - 2)));
    const int zi = Rgb2SpecDetail::findInterval(model.scale, res, z);
    const int offset = (((dominant * res + zi) * res + yi) * res + xi) * Rgb2SpecDetail::kCoeffCount;
    const int dx = Rgb2SpecDetail::kCoeffCount;
    const int dy = Rgb2SpecDetail::kCoeffCount * res;
    const int dz = Rgb2SpecDetail::kCoeffCount * res * res;

    const float x1 = x - static_cast<float>(xi);
    const float x0 = 1.0f - x1;
    const float y1 = y - static_cast<float>(yi);
    const float y0 = 1.0f - y1;
    const float z1 = (z - model.scale[zi]) / vecMax2(model.scale[zi + 1] - model.scale[zi], 1.0e-12f);
    const float z0 = 1.0f - z1;

    for (int coeffIndex = 0; coeffIndex < Rgb2SpecDetail::kCoeffCount; ++coeffIndex) {
        const int base = offset + coeffIndex;
        const float c000 = model.data[base];
        const float c100 = model.data[base + dx];
        const float c010 = model.data[base + dy];
        const float c110 = model.data[base + dy + dx];
        const float c001 = model.data[base + dz];
        const float c101 = model.data[base + dz + dx];
        const float c011 = model.data[base + dz + dy];
        const float c111 = model.data[base + dz + dy + dx];

        out[coeffIndex] =
            ((c000 * x0 + c100 * x1) * y0 + (c010 * x0 + c110 * x1) * y1) * z0 +
            ((c001 * x0 + c101 * x1) * y0 + (c011 * x0 + c111 * x1) * y1) * z1;
    }
}

RGB2SPEC_FN float rgb2specEvalReflectance(
    const Rgb2SpecGpu& model,
    float r,
    float g,
    float b,
    float lambdaNm)
{
    float rgb[3] = {r, g, b};
    float coeff[Rgb2SpecDetail::kCoeffCount]{};
    rgb2specFetchCoefficients(model, rgb, coeff);
    return rgb2specEvalCoefficients(coeff, lambdaNm);
}

RGB2SPEC_FN float rgb2specEvalRadiance(
    const Rgb2SpecGpu& model,
    float r,
    float g,
    float b,
    float lambdaNm)
{
    const float maxChannel = vecMax2(r, vecMax2(g, b));
    if (maxChannel <= 1.0e-8f) {
        return 0.0f;
    }

    float scale = 1.0f;
    float rgb[3] = {r, g, b};
    if (maxChannel > 1.0f) {
        scale = maxChannel;
        rgb[0] /= scale;
        rgb[1] /= scale;
        rgb[2] /= scale;
    }

    float coeff[Rgb2SpecDetail::kCoeffCount]{};
    rgb2specFetchCoefficients(model, rgb, coeff);
    return scale * rgb2specEvalCoefficients(coeff, lambdaNm);
}

#undef RGB2SPEC_FN
