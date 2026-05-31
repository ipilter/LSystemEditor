#pragma once

#include "SdfTypes.h"

#include <cuda_runtime.h>
#include <vector_types.h>

class SdfRayMarcher
{
public:
    SdfRayMarcher();
    ~SdfRayMarcher();

    SdfRayMarcher(const SdfRayMarcher&) = delete;
    SdfRayMarcher& operator=(const SdfRayMarcher&) = delete;

    void setMarchParams(const SdfMarchParamsHost& params);
    void setDefaultLayout();

    bool allocate();
    void release();

    bool upload(cudaStream_t stream);

    const SdfSceneGpu* deviceScene() const { return m_dScene; }
    const SdfMarchParamsGpu* deviceMarchParams() const { return m_dMarchParams; }

private:
    SdfSceneGpu m_hostScene{};
    SdfMarchParamsHost m_hostMarchParams{};
    SdfSceneGpu* m_dScene = nullptr;
    SdfMarchParamsGpu* m_dMarchParams = nullptr;
    bool m_sceneDirty = true;
    bool m_paramsDirty = true;
};
