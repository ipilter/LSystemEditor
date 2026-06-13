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
    float sunAzimuthDeg = 45.0f;
    float sunElevationDeg = 45.0f;
    float sunDiskSizeDeg = 0.5f;
    float sunColorR = 1.0f;
    float sunColorG = 1.0f;
    float sunColorB = 1.0f;
    float sunIntensity = 1.0f;
    int maxPathDepth = 8;
    int russianRouletteMinDepth = 3;
    unsigned int globalSeed = 0;
};
