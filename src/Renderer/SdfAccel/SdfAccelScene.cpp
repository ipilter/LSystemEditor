#include "SdfAccelScene.h"

#include "SdfAccelTraverseCore.h"
#include "SdfBvhBuilder.h"
#include "SdfSceneContent.h"

#include <cuda_runtime.h>
#include <type_traits>

SdfAccelScene::SdfAccelScene() = default;

SdfAccelScene::~SdfAccelScene()
{
    release();
}

void SdfAccelScene::clear()
{
    m_pendingObjects.clear();
    m_bvhNodes.clear();
    m_objects.clear();
    m_payloads.clear();
    m_hostScene = SdfAccelSceneGpu{};
    m_built = false;
    m_deviceDirty = true;
}

void SdfAccelScene::setBuildParams(const SdfAccelBuildParams& params)
{
    m_buildParams = params;
}

void SdfAccelScene::setDefaultLayout()
{
    sdfAccelPopulateScene(*this, sdfDefaultSceneShapes());
}

void SdfAccelScene::addShape(const SdfShape& shape)
{
    PendingObject object{};
    object.field = shape.buildAccelField();
    m_pendingObjects.push_back(object);
    m_built = false;
    m_deviceDirty = true;
}

bool SdfAccelScene::build()
{
    if (m_pendingObjects.empty()) {
        m_built = false;
        return false;
    }

    m_bvhNodes.clear();
    m_objects.clear();
    m_payloads.clear();
    m_hostScene = SdfAccelSceneGpu{};
    m_built = false;
    m_deviceDirty = true;

    m_payloads.reserve(m_pendingObjects.size());
    m_objects.reserve(m_pendingObjects.size());

    for (size_t i = 0; i < m_pendingObjects.size(); ++i) {
        const SdfAccelField& field = m_pendingObjects[i].field;
        m_payloads.push_back(field.payload);

        SdfAccelObjectGpu objectGpu{};
        objectGpu.payloadIndex = static_cast<uint32_t>(i);
        objectGpu.center = field.worldCenter;
        objectGpu.boundsMin = field.localBoundsMin;
        objectGpu.boundsMax = field.localBoundsMax;

        const SdfAccelPayloadGpu& payload = m_payloads.back();
        if (sdfAccelPayloadIsAnalytical(&payload)) {
            objectGpu.flags = SdfObjectFlagAnalytical;
        }

        m_objects.push_back(objectGpu);
    }

    uint32_t bvhRootIndex = 0;
    if (!sdfAccelBuildBvh(m_objects, m_bvhNodes, bvhRootIndex)) {
        return false;
    }

    m_hostScene = SdfAccelSceneGpu{};
    m_hostScene.bvhNodes = m_bvhNodes.data();
    m_hostScene.objects = m_objects.data();
    m_hostScene.payloads = m_payloads.data();
    m_hostScene.bvhNodeCount = static_cast<uint32_t>(m_bvhNodes.size());
    m_hostScene.objectCount = static_cast<uint32_t>(m_objects.size());
    m_hostScene.payloadCount = static_cast<uint32_t>(m_payloads.size());
    m_hostScene.bvhRootIndex = bvhRootIndex;

    m_built = true;
    m_deviceDirty = true;
    return true;
}

bool SdfAccelScene::allocate()
{
    release();

    if (cudaMalloc(&m_dScene, sizeof(SdfAccelSceneGpu)) != cudaSuccess) {
        release();
        return false;
    }

    m_deviceDirty = true;
    return true;
}

void SdfAccelScene::release()
{
    if (m_dBvhNodes != nullptr) {
        cudaFree(m_dBvhNodes);
        m_dBvhNodes = nullptr;
    }
    if (m_dObjects != nullptr) {
        cudaFree(m_dObjects);
        m_dObjects = nullptr;
    }
    if (m_dPayloads != nullptr) {
        cudaFree(m_dPayloads);
        m_dPayloads = nullptr;
    }
    if (m_dScene != nullptr) {
        cudaFree(m_dScene);
        m_dScene = nullptr;
    }
    m_deviceDirty = true;
}

bool SdfAccelScene::upload(cudaStream_t stream)
{
    if (m_dScene == nullptr || !m_built) {
        return false;
    }

    if (!m_deviceDirty && m_dBvhNodes != nullptr) {
        return true;
    }

    if (m_dBvhNodes != nullptr) {
        cudaFree(m_dBvhNodes);
        m_dBvhNodes = nullptr;
    }
    if (m_dObjects != nullptr) {
        cudaFree(m_dObjects);
        m_dObjects = nullptr;
    }
    if (m_dPayloads != nullptr) {
        cudaFree(m_dPayloads);
        m_dPayloads = nullptr;
    }

    auto allocAndCopy = [stream](auto** devicePtr, const auto& hostData) -> bool {
        using ValueType = typename std::decay<decltype(hostData[0])>::type;
        if (hostData.empty()) {
            *devicePtr = nullptr;
            return true;
        }

        const size_t bytes = hostData.size() * sizeof(ValueType);
        if (cudaMalloc(devicePtr, bytes) != cudaSuccess) {
            return false;
        }
        return cudaMemcpyAsync(*devicePtr, hostData.data(), bytes, cudaMemcpyHostToDevice, stream) == cudaSuccess;
    };

    if (!allocAndCopy(&m_dBvhNodes, m_bvhNodes)) {
        release();
        return false;
    }
    if (!allocAndCopy(&m_dObjects, m_objects)) {
        release();
        return false;
    }
    if (!allocAndCopy(&m_dPayloads, m_payloads)) {
        release();
        return false;
    }

    SdfAccelSceneGpu deviceScene{};
    deviceScene.bvhNodes = m_dBvhNodes;
    deviceScene.objects = m_dObjects;
    deviceScene.payloads = m_dPayloads;
    deviceScene.bvhNodeCount = static_cast<uint32_t>(m_bvhNodes.size());
    deviceScene.objectCount = static_cast<uint32_t>(m_objects.size());
    deviceScene.payloadCount = static_cast<uint32_t>(m_payloads.size());
    deviceScene.bvhRootIndex = m_hostScene.bvhRootIndex;

    if (cudaMemcpyAsync(m_dScene, &deviceScene, sizeof(SdfAccelSceneGpu), cudaMemcpyHostToDevice, stream)
        != cudaSuccess) {
        if (m_dBvhNodes != nullptr) {
            cudaFree(m_dBvhNodes);
            m_dBvhNodes = nullptr;
        }
        if (m_dObjects != nullptr) {
            cudaFree(m_dObjects);
            m_dObjects = nullptr;
        }
        if (m_dPayloads != nullptr) {
            cudaFree(m_dPayloads);
            m_dPayloads = nullptr;
        }
        return false;
    }

    m_deviceDirty = false;
    return true;
}
