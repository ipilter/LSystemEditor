#include "Spectral/SpectralState.h"

#include "Spectral/SpectralCore.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <cuda_runtime.h>

namespace {

Rgb2SpecGpu gHostSpectralModel{};

void computeWhiteNormalization(Rgb2SpecGpu& model)
{
    float sumR = 0.0f;
    float sumG = 0.0f;
    float sumB = 0.0f;

    for (int lambda = static_cast<int>(SpectralDetail::kLambdaMin);
         lambda <= static_cast<int>(SpectralDetail::kLambdaMax);
         ++lambda) {
        const float spectrum = rgb2specEvalReflectance(
            model,
            1.0f,
            1.0f,
            1.0f,
            static_cast<float>(lambda));
        float responseR = 0.0f;
        float responseG = 0.0f;
        float responseB = 0.0f;
        SpectralDetail::spectralRgbResponseAtWavelength(
            static_cast<float>(lambda),
            responseR,
            responseG,
            responseB);
        sumR += spectrum * responseR;
        sumG += spectrum * responseG;
        sumB += spectrum * responseB;
    }

    model.whiteNormR = vecMax2(sumR, 1.0e-8f);
    model.whiteNormG = vecMax2(sumG, 1.0e-8f);
    model.whiteNormB = vecMax2(sumB, 1.0e-8f);
}

} // namespace

bool SpectralStateHost::loadCoeffFile(const std::string& path, std::string* outError)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        if (outError != nullptr) {
            *outError = "failed to open rgb2spec coefficient file: " + path;
        }
        return false;
    }

    char header[4] = {};
    if (std::fread(header, 1, 4, file) != 4 || std::memcmp(header, "SPEC", 4) != 0) {
        std::fclose(file);
        if (outError != nullptr) {
            *outError = "invalid rgb2spec coefficient header";
        }
        return false;
    }

    uint32_t resolution = 0;
    if (std::fread(&resolution, sizeof(uint32_t), 1, file) != 1 || resolution < 2) {
        std::fclose(file);
        if (outError != nullptr) {
            *outError = "invalid rgb2spec coefficient resolution";
        }
        return false;
    }

    scale.resize(resolution);
    if (std::fread(scale.data(), sizeof(float), resolution, file) != resolution) {
        std::fclose(file);
        if (outError != nullptr) {
            *outError = "failed to read rgb2spec scale table";
        }
        return false;
    }

    const size_t dataCount = static_cast<size_t>(3) * 3 * resolution * resolution * resolution;
    data.resize(dataCount);
    if (std::fread(data.data(), sizeof(float), dataCount, file) != dataCount) {
        std::fclose(file);
        if (outError != nullptr) {
            *outError = "failed to read rgb2spec coefficient data";
        }
        return false;
    }

    std::fclose(file);
    res = static_cast<int>(resolution);

    Rgb2SpecGpu model = hostModel();
    computeWhiteNormalization(model);
    whiteNormR = model.whiteNormR;
    whiteNormG = model.whiteNormG;
    whiteNormB = model.whiteNormB;
    spectralSetHostModel(model);
    return true;
}

bool SpectralStateHost::uploadToDevice(std::string* outError)
{
    freeDevice();

    if (scale.empty() || data.empty() || res <= 1) {
        if (outError != nullptr) {
            *outError = "rgb2spec host tables are empty";
        }
        return false;
    }

    const size_t scaleBytes = scale.size() * sizeof(float);
    const size_t dataBytes = data.size() * sizeof(float);

    if (cudaMalloc(&d_scale, scaleBytes) != cudaSuccess ||
        cudaMalloc(&d_data, dataBytes) != cudaSuccess) {
        freeDevice();
        if (outError != nullptr) {
            *outError = "cudaMalloc failed for rgb2spec tables";
        }
        return false;
    }

    if (cudaMemcpy(d_scale, scale.data(), scaleBytes, cudaMemcpyHostToDevice) != cudaSuccess ||
        cudaMemcpy(d_data, data.data(), dataBytes, cudaMemcpyHostToDevice) != cudaSuccess) {
        freeDevice();
        if (outError != nullptr) {
            *outError = "cudaMemcpy failed for rgb2spec tables";
        }
        return false;
    }

    SpectralStateGpu deviceState{};
    deviceState.rgb2specScale = d_scale;
    deviceState.rgb2specData = d_data;
    deviceState.rgb2specRes = res;
    deviceState.whiteNormR = whiteNormR;
    deviceState.whiteNormG = whiteNormG;
    deviceState.whiteNormB = whiteNormB;

    if (const cudaError_t uploadError = spectralUploadStateToSymbol(&deviceState);
        uploadError != cudaSuccess) {
        freeDevice();
        if (outError != nullptr) {
            *outError = std::string("cudaMemcpyToSymbol failed for spectral state: ") +
                cudaGetErrorString(uploadError);
        }
        return false;
    }

    return true;
}

void SpectralStateHost::freeDevice()
{
    if (d_scale != nullptr) {
        cudaFree(d_scale);
        d_scale = nullptr;
    }
    if (d_data != nullptr) {
        cudaFree(d_data);
        d_data = nullptr;
    }
}

Rgb2SpecGpu spectralHostModel()
{
    return gHostSpectralModel;
}

void spectralSetHostModel(const Rgb2SpecGpu& model)
{
    gHostSpectralModel = model;
}

bool spectralInitHostFromCoeffFile(const char* coeffPath, std::string* outError)
{
    static SpectralStateHost state;
    if (!state.loadCoeffFile(coeffPath, outError)) {
        return false;
    }
    spectralSetHostModel(state.hostModel());
    return true;
}
