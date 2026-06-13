#pragma once

#include "MeshAccel/MeshAccelTypes.h"
#include "RenderTypes.h"

#include <cuda_runtime.h>

#include <QString>
#include <vector>

class EnvironmentMap
{
public:
    EnvironmentMap();
    ~EnvironmentMap();

    EnvironmentMap(const EnvironmentMap&) = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;

    void release();

    bool loadFromHdr(const QString& path, QString* outError = nullptr);
    void clear();

    bool upload(cudaStream_t stream);
    const EnvironmentMapGpu* deviceMap() const { return m_dMap; }
    const EnvironmentMapGpu& hostMap() const { return m_hostMap; }
    bool isValid() const { return m_hostMap.valid != 0; }

private:
    bool buildImportanceTables();

    std::vector<float> m_pixels;
    std::vector<float> m_marginalCdf;
    std::vector<float> m_rowCdf;
    int m_width = 0;
    int m_height = 0;

    EnvironmentMapGpu m_hostMap{};
    EnvironmentMapGpu* m_dMap = nullptr;
    float* m_dPixels = nullptr;
    float* m_dMarginalCdf = nullptr;
    float* m_dRowCdf = nullptr;
    bool m_deviceDirty = true;
};
