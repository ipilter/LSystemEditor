#pragma once

#include "PathTracerAdaptiveSampling.h"

/// Host-side sample budget helpers (preview pyramid + per-pixel max).
/// Total kernel launches while accumulating: previewSteps + maxSamplesPerPixel (0 = unlimited).

inline int sampleBudgetTotalIterations(int previewSteps, int maxSamplesPerPixel)
{
    if (maxSamplesPerPixel <= 0) {
        return -1;
    }
    return previewSteps + maxSamplesPerPixel;
}

inline bool canTakeSampleAtIteration(int sampleCount, int previewSteps, int maxSamplesPerPixel)
{
    if (maxSamplesPerPixel <= 0) {
        return true;
    }
    return sampleCount < previewSteps + maxSamplesPerPixel;
}

inline bool isSampleBudgetExhausted(int sampleCount, int previewSteps, int maxSamplesPerPixel)
{
    if (maxSamplesPerPixel <= 0) {
        return false;
    }
    return sampleCount >= previewSteps + maxSamplesPerPixel;
}

/// Adaptive full-resolution phase: stop when no pixels remain active.
inline bool isAdaptiveSampleBudgetExhausted(int activePixelCount, int previewSteps, int sampleCount)
{
    if (sampleCount < previewSteps) {
        return false;
    }
    return isAdaptiveBudgetExhausted(activePixelCount);
}
