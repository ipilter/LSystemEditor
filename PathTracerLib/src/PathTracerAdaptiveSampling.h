#pragma once

#include <cmath>
#include <cstdint>

inline constexpr int kAdaptiveConvergenceCheckInterval = 4;
inline constexpr int kAdaptiveCompactActiveListInterval = 4;
inline constexpr float kAdaptiveLuminanceEpsilon = 1.0e-4f;
inline constexpr float kAdaptiveMinLuminanceForConvergence = 1.0e-5f;

inline float sampleLuminance(float r, float g, float b)
{
    return r * 0.2126f + g * 0.7152f + b * 0.0722f;
}

inline bool isAdaptivePixelConverged(
    uint32_t sampleCount,
    float lumMean,
    float m2,
    int minSamples,
    float relativeErrorThreshold)
{
    if (sampleCount < static_cast<uint32_t>(minSamples)) {
        return false;
    }

    if (sampleCount == 0) {
        return false;
    }

    if (lumMean < kAdaptiveMinLuminanceForConvergence) {
        return false;
    }

    const float variance = m2 / static_cast<float>(sampleCount);
    const float stdDev = std::sqrt(variance);
    const float denom = std::fmax(lumMean, kAdaptiveLuminanceEpsilon);
    const float relativeError = stdDev / denom;
    return relativeError < relativeErrorThreshold;
}

inline bool isAdaptiveBudgetExhausted(int activePixelCount)
{
    return activePixelCount <= 0;
}
