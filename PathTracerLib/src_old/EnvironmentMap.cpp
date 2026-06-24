#include "EnvironmentMap.h"

#include <cuda_runtime.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>

namespace {

template<typename T>
bool uploadVector(T** devicePtr, const std::vector<T>& hostData, cudaStream_t stream)
{
    if (devicePtr == nullptr) {
        return false;
    }

    if (hostData.empty()) {
        *devicePtr = nullptr;
        return true;
    }

    const size_t bytes = hostData.size() * sizeof(T);
    void* allocation = nullptr;
    if (cudaMalloc(&allocation, bytes) != cudaSuccess || allocation == nullptr) {
        return false;
    }

    *devicePtr = static_cast<T*>(allocation);
    return cudaMemcpyAsync(*devicePtr, hostData.data(), bytes, cudaMemcpyHostToDevice, stream) == cudaSuccess;
}

} // namespace

EnvironmentMap::EnvironmentMap() = default;

EnvironmentMap::~EnvironmentMap()
{
    release();
}

void EnvironmentMap::release()
{
    if (m_dPixels != nullptr) {
        cudaFree(m_dPixels);
        m_dPixels = nullptr;
    }
    if (m_dMarginalCdf != nullptr) {
        cudaFree(m_dMarginalCdf);
        m_dMarginalCdf = nullptr;
    }
    if (m_dRowCdf != nullptr) {
        cudaFree(m_dRowCdf);
        m_dRowCdf = nullptr;
    }
    if (m_dMap != nullptr) {
        cudaFree(m_dMap);
        m_dMap = nullptr;
    }
    m_deviceDirty = true;
}

void EnvironmentMap::clear()
{
    m_pixels.clear();
    m_marginalCdf.clear();
    m_rowCdf.clear();
    m_width = 0;
    m_height = 0;
    m_hostMap = EnvironmentMapGpu{};
    m_deviceDirty = true;
}

bool EnvironmentMap::buildImportanceTables()
{
    if (m_width <= 0 || m_height <= 0 || m_pixels.empty()) {
        return false;
    }

    m_marginalCdf.assign(static_cast<std::size_t>(m_height) + 1, 0.0f);
    m_rowCdf.assign(static_cast<std::size_t>(m_height) * static_cast<std::size_t>(m_width + 1), 0.0f);

    std::vector<float> rowWeights(static_cast<std::size_t>(m_height), 0.0f);
    for (int y = 0; y < m_height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(m_height);
        const float sinTheta = std::sin(v * 3.14159265f);
        float rowSum = 0.0f;
        const std::size_t rowOffset = static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width + 1);
        m_rowCdf[rowOffset] = 0.0f;
        for (int x = 0; x < m_width; ++x) {
            const int index = (y * m_width + x) * 3;
            const float luminance = std::max(
                m_pixels[static_cast<std::size_t>(index)],
                std::max(m_pixels[static_cast<std::size_t>(index + 1)], m_pixels[static_cast<std::size_t>(index + 2)]));
            const float weight = std::max(luminance, 1.0e-6f) * sinTheta;
            rowSum += weight;
            m_rowCdf[rowOffset + static_cast<std::size_t>(x) + 1] = rowSum;
        }
        if (rowSum > 0.0f) {
            for (int x = 0; x < m_width; ++x) {
                m_rowCdf[rowOffset + static_cast<std::size_t>(x) + 1] /= rowSum;
            }
        }
        rowWeights[static_cast<std::size_t>(y)] = rowSum;
    }

    float total = 0.0f;
    for (int y = 0; y < m_height; ++y) {
        total += rowWeights[static_cast<std::size_t>(y)];
        m_marginalCdf[static_cast<std::size_t>(y) + 1] = total;
    }
    if (total > 0.0f) {
        for (int y = 0; y < m_height; ++y) {
            m_marginalCdf[static_cast<std::size_t>(y) + 1] /= total;
        }
    }

    m_hostMap.pixels = m_pixels.data();
    m_hostMap.marginalCdf = m_marginalCdf.data();
    m_hostMap.rowCdf = m_rowCdf.data();
    m_hostMap.width = m_width;
    m_hostMap.height = m_height;
    m_hostMap.valid = 1;
    return true;
}

float EnvironmentMap::estimateLuminancePercentile(float percentile) const
{
    if (m_pixels.empty()) {
        return 0.0f;
    }

    const float clampedPercentile = std::max(0.0f, std::min(percentile, 1.0f));
    std::vector<float> luminances;
    luminances.reserve(m_pixels.size() / 3);
    for (std::size_t i = 0; i + 2 < m_pixels.size(); i += 3) {
        const float r = m_pixels[i];
        const float g = m_pixels[i + 1];
        const float b = m_pixels[i + 2];
        luminances.push_back(0.2126f * r + 0.7152f * g + 0.0722f * b);
    }

    if (luminances.empty()) {
        return 0.0f;
    }

    const std::size_t index = static_cast<std::size_t>(
        clampedPercentile * static_cast<float>(luminances.size() - 1));
    std::nth_element(luminances.begin(), luminances.begin() + static_cast<std::ptrdiff_t>(index), luminances.end());
    return luminances[index];
}

float EnvironmentMap::estimateLogAverageLuminance() const
{
    if (m_pixels.empty()) {
        return 0.0f;
    }

    double logSum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i + 2 < m_pixels.size(); i += 3) {
        const float r = m_pixels[i];
        const float g = m_pixels[i + 1];
        const float b = m_pixels[i + 2];
        const float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        if (luminance > 1.0e-6f) {
            logSum += std::log(static_cast<double>(luminance));
            ++count;
        }
    }

    if (count == 0) {
        return 0.0f;
    }

    return static_cast<float>(std::exp(logSum / static_cast<double>(count)));
}

PhysicalCamera EnvironmentMap::suggestPhysicalCamera() const
{
    if (!isValid()) {
        return PhysicalCamera{};
    }

    const float logAverage = estimateLogAverageLuminance();
    return PhysicalCamera::forAverageLuminance(logAverage);
}

bool EnvironmentMap::loadFromHdr(const QString& path, QString* outError)
{
    clear();

    const QByteArray pathUtf8 = path.toUtf8();
    int width = 0;
    int height = 0;
    int channels = 0;
    float* image = stbi_loadf(pathUtf8.constData(), &width, &height, &channels, 3);
    if (image == nullptr) {
        if (outError != nullptr) {
            *outError = QStringLiteral("Failed to load HDR image: %1").arg(path);
        }
        return false;
    }

    m_width = width;
    m_height = height;
    m_pixels.assign(image, image + static_cast<std::size_t>(width * height * 3));
    stbi_image_free(image);

    if (!buildImportanceTables()) {
        clear();
        if (outError != nullptr) {
            *outError = QStringLiteral("Failed to build environment importance tables");
        }
        return false;
    }

    m_deviceDirty = true;
    return true;
}

bool EnvironmentMap::upload(cudaStream_t stream)
{
    if (!isValid()) {
        if (m_dMap != nullptr) {
            EnvironmentMapGpu empty{};
            return cudaMemcpyAsync(m_dMap, &empty, sizeof(EnvironmentMapGpu), cudaMemcpyHostToDevice, stream)
                == cudaSuccess;
        }
        return true;
    }

    if (m_dMap == nullptr) {
        if (cudaMalloc(&m_dMap, sizeof(EnvironmentMapGpu)) != cudaSuccess) {
            return false;
        }
        m_deviceDirty = true;
    }

    if (!m_deviceDirty) {
        return true;
    }

    if (m_dPixels != nullptr) {
        cudaFree(m_dPixels);
        m_dPixels = nullptr;
    }
    if (m_dMarginalCdf != nullptr) {
        cudaFree(m_dMarginalCdf);
        m_dMarginalCdf = nullptr;
    }
    if (m_dRowCdf != nullptr) {
        cudaFree(m_dRowCdf);
        m_dRowCdf = nullptr;
    }

    if (!uploadVector(&m_dPixels, m_pixels, stream) || !uploadVector(&m_dMarginalCdf, m_marginalCdf, stream) ||
        !uploadVector(&m_dRowCdf, m_rowCdf, stream)) {
        release();
        return false;
    }

    EnvironmentMapGpu deviceMap{};
    deviceMap.pixels = m_dPixels;
    deviceMap.marginalCdf = m_dMarginalCdf;
    deviceMap.rowCdf = m_dRowCdf;
    deviceMap.width = m_width;
    deviceMap.height = m_height;
    deviceMap.valid = 1;

    if (cudaMemcpyAsync(m_dMap, &deviceMap, sizeof(EnvironmentMapGpu), cudaMemcpyHostToDevice, stream) != cudaSuccess) {
        release();
        return false;
    }

    m_deviceDirty = false;
    return true;
}
