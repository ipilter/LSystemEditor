#pragma once

#include "Rgb2Spec.h"

#include <cuda_runtime.h>

#include <cstdint>
#include <string>
#include <vector>

struct SpectralStateGpu
{
    const float* rgb2specScale = nullptr;
    const float* rgb2specData = nullptr;
    int rgb2specRes = 0;
    float whiteNormR = 1.0f;
    float whiteNormG = 1.0f;
    float whiteNormB = 1.0f;
};

struct SpectralStateHost
{
    std::vector<float> scale;
    std::vector<float> data;
    int res = 0;
    float whiteNormR = 1.0f;
    float whiteNormG = 1.0f;
    float whiteNormB = 1.0f;

    float* d_scale = nullptr;
    float* d_data = nullptr;

    Rgb2SpecGpu hostModel() const
    {
        Rgb2SpecGpu model{};
        model.scale = scale.empty() ? nullptr : scale.data();
        model.data = data.empty() ? nullptr : data.data();
        model.res = res;
        model.whiteNormR = whiteNormR;
        model.whiteNormG = whiteNormG;
        model.whiteNormB = whiteNormB;
        return model;
    }

    bool loadCoeffFile(const std::string& path, std::string* outError = nullptr);
    bool uploadToDevice(std::string* outError = nullptr);
    void freeDevice();
};

Rgb2SpecGpu spectralHostModel();
void spectralSetHostModel(const Rgb2SpecGpu& model);
bool spectralInitHostFromCoeffFile(const char* coeffPath, std::string* outError = nullptr);

extern "C" cudaError_t spectralUploadStateToSymbol(const SpectralStateGpu* hostState);
