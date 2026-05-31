#include "SdfRayMarcher.h"

#include <cuda_runtime.h>

namespace {

constexpr float kPrimitiveRadius = 0.5f;
constexpr float kPrimitiveHalfHeight = 1.0f;
constexpr float kLayoutSpacing = 2.5f;

} // namespace

SdfRayMarcher::SdfRayMarcher()
{
    m_hostMarchParams = SdfMarchParamsHost{};
    m_hostMarchParams.maxDistance = 100.0f;
    m_hostMarchParams.surfaceEpsilon = 1.0e-4f;
    m_hostMarchParams.normalEpsilon = 1.0e-4f;
    m_hostMarchParams.maxSteps = 256;
    m_hostMarchParams.refineIterations = 10;

    setDefaultLayout();
}

SdfRayMarcher::~SdfRayMarcher()
{
    release();
}

void SdfRayMarcher::setMarchParams(const SdfMarchParamsHost& params)
{
    m_hostMarchParams = params;
    m_paramsDirty = true;
}

void SdfRayMarcher::setDefaultLayout()
{
    m_hostScene = SdfSceneGpu{};

    m_hostScene.cylinderCenter = SdfFloat3{-kLayoutSpacing, 0.0f, 0.0f};
    m_hostScene.cylinderHalfExtents = SdfFloat2{kPrimitiveRadius, kPrimitiveHalfHeight};

    m_hostScene.sphereCenter = SdfFloat3{0.0f, 0.0f, 0.0f};
    m_hostScene.sphereRadius = kPrimitiveRadius;

    m_hostScene.coneCenter = SdfFloat3{kLayoutSpacing, 0.0f, 0.0f};
    m_hostScene.coneHalfHeight = kPrimitiveHalfHeight;
    m_hostScene.coneRadiusBottom = kPrimitiveRadius;
    m_hostScene.coneRadiusTop = kPrimitiveRadius * 0.25f;

    m_sceneDirty = true;
}

bool SdfRayMarcher::allocate()
{
    release();

    if (cudaMalloc(&m_dScene, sizeof(SdfSceneGpu)) != cudaSuccess) {
        release();
        return false;
    }
    if (cudaMalloc(&m_dMarchParams, sizeof(SdfMarchParamsGpu)) != cudaSuccess) {
        release();
        return false;
    }

    m_sceneDirty = true;
    m_paramsDirty = true;
    return true;
}

void SdfRayMarcher::release()
{
    if (m_dScene != nullptr) {
        cudaFree(m_dScene);
        m_dScene = nullptr;
    }
    if (m_dMarchParams != nullptr) {
        cudaFree(m_dMarchParams);
        m_dMarchParams = nullptr;
    }
}

bool SdfRayMarcher::upload(cudaStream_t stream)
{
    if (m_dScene == nullptr || m_dMarchParams == nullptr) {
        return false;
    }

    if (m_sceneDirty) {
        const cudaError_t error = cudaMemcpyAsync(
            m_dScene, &m_hostScene, sizeof(SdfSceneGpu), cudaMemcpyHostToDevice, stream);
        if (error != cudaSuccess) {
            return false;
        }
        m_sceneDirty = false;
    }

    if (m_paramsDirty) {
        const cudaError_t error = cudaMemcpyAsync(
            m_dMarchParams,
            &m_hostMarchParams,
            sizeof(SdfMarchParamsGpu),
            cudaMemcpyHostToDevice,
            stream);
        if (error != cudaSuccess) {
            return false;
        }
        m_paramsDirty = false;
    }

    return true;
}
