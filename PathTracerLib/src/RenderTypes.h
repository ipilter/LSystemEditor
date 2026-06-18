#pragma once

#include <cstdint>

struct EnvironmentMapGpu
{
    const float* pixels = nullptr;
    const float* marginalCdf = nullptr;
    const float* rowCdf = nullptr;
    int width = 0;
    int height = 0;
    int valid = 0;
};

struct RenderParamsGpu
{
    float backgroundR = 10.0f / 255.0f;
    float backgroundG = 10.0f / 255.0f;
    float backgroundB = 10.0f / 255.0f;
    float environmentIntensity = 1.0f;
    int maxPathDepth = 32;
    int russianRouletteMinDepth = 3;
    int minSamples = 16;
    int maxSamplesPerPixel = 1024;
    int debugOverlayMode = 0;
    float relativeErrorThreshold = 0.02f;
};
