#include "MeshAccelScene.h"

#include "MeshBvhBuilder.h"
#include "Geometry/MathCore.h"

#include <cuda_runtime.h>
#include <type_traits>

namespace {

TriangleGpu makeTriangleGpu(const HostTriangle& tri)
{
    TriangleGpu gpu{};
    gpu.v0 = tri.v0;
    gpu.v1 = tri.v1;
    gpu.v2 = tri.v2;
    gpu.n0 = tri.n0;
    gpu.n1 = tri.n1;
    gpu.n2 = tri.n2;
    gpu.materialIndex = tri.materialIndex;
    return gpu;
}

MaterialGpu defaultMaterial()
{
    MaterialGpu material{};
    material.r = 0.8f;
    material.g = 0.8f;
    material.b = 0.8f;
    material.roughness = 0.5f;
    material.metallic = 0.0f;
    material.emission = 0.0f;
    material.ior = 1.5f;
    material.transmission = 0.0f;
    return material;
}

void buildEmissiveTriangleList(
    const std::vector<TriangleGpu>& triangles,
    const std::vector<MaterialGpu>& materials,
    std::vector<uint32_t>& outIndices)
{
    outIndices.clear();
    for (uint32_t triIndex = 0; triIndex < triangles.size(); ++triIndex) {
        const TriangleGpu& tri = triangles[triIndex];
        if (tri.materialIndex >= materials.size()) {
            continue;
        }
        const MaterialGpu& material = materials[tri.materialIndex];
        if (material.emission > 0.0f) {
            outIndices.push_back(triIndex);
        }
    }
}

template<typename T>
bool uploadHostVector(T** devicePtr, const std::vector<T>& hostData, cudaStream_t stream)
{
    if (devicePtr == nullptr) {
        return false;
    }

    if (hostData.empty()) {
        *devicePtr = nullptr;
        return true;
    }

    const size_t bytes = hostData.size() * sizeof(T);
    void* deviceAllocation = nullptr;
    if (cudaMalloc(&deviceAllocation, bytes) != cudaSuccess || deviceAllocation == nullptr) {
        return false;
    }

    *devicePtr = static_cast<T*>(deviceAllocation);
    return cudaMemcpyAsync(*devicePtr, hostData.data(), bytes, cudaMemcpyHostToDevice, stream) == cudaSuccess;
}

} // namespace

MeshAccelScene::MeshAccelScene() = default;

MeshAccelScene::~MeshAccelScene()
{
    release();
}

void MeshAccelScene::clear()
{
    m_bvhNodes.clear();
    m_triangles.clear();
    m_materials.clear();
    m_emissiveTriangleIndices.clear();
    m_hostScene = MeshAccelSceneGpu{};
    m_built = false;
    m_deviceDirty = true;
}

bool MeshAccelScene::build(const HostMesh& mesh)
{
    clear();

    if (mesh.triangles.empty()) {
        m_hostScene = MeshAccelSceneGpu{};
        m_hostScene.bvhNodeCount = 0;
        m_hostScene.triangleCount = 0;
        m_hostScene.bvhRootIndex = 0;
        m_hostScene.materialCount = 0;
        m_built = true;
        m_deviceDirty = true;
        return true;
    }

    m_materials = mesh.materials;
    if (m_materials.empty()) {
        m_materials.push_back(defaultMaterial());
    }

    m_triangles.reserve(mesh.triangles.size());
    for (const HostTriangle& tri : mesh.triangles) {
        m_triangles.push_back(makeTriangleGpu(tri));
    }

    uint32_t bvhRootIndex = 0;
    if (!meshAccelBuildBvh(m_triangles, m_bvhNodes, bvhRootIndex)) {
        m_triangles.clear();
        m_materials.clear();
        return false;
    }

    buildEmissiveTriangleList(m_triangles, m_materials, m_emissiveTriangleIndices);

    m_hostScene = MeshAccelSceneGpu{};
    m_hostScene.bvhNodes = m_bvhNodes.data();
    m_hostScene.triangles = m_triangles.data();
    m_hostScene.materials = m_materials.data();
    m_hostScene.emissiveTriangleIndices = m_emissiveTriangleIndices.data();
    m_hostScene.bvhNodeCount = static_cast<uint32_t>(m_bvhNodes.size());
    m_hostScene.triangleCount = static_cast<uint32_t>(m_triangles.size());
    m_hostScene.bvhRootIndex = bvhRootIndex;
    m_hostScene.materialCount = static_cast<uint32_t>(m_materials.size());
    m_hostScene.emissiveTriangleCount = static_cast<uint32_t>(m_emissiveTriangleIndices.size());

    m_built = true;
    m_deviceDirty = true;
    return true;
}

bool MeshAccelScene::allocate()
{
    release();

    if (cudaMalloc(&m_dScene, sizeof(MeshAccelSceneGpu)) != cudaSuccess) {
        release();
        return false;
    }

    m_deviceDirty = true;
    return true;
}

void MeshAccelScene::release()
{
    if (m_dBvhNodes != nullptr) {
        cudaFree(m_dBvhNodes);
        m_dBvhNodes = nullptr;
    }
    if (m_dTriangles != nullptr) {
        cudaFree(m_dTriangles);
        m_dTriangles = nullptr;
    }
    if (m_dMaterials != nullptr) {
        cudaFree(m_dMaterials);
        m_dMaterials = nullptr;
    }
    if (m_dEmissiveTriangleIndices != nullptr) {
        cudaFree(m_dEmissiveTriangleIndices);
        m_dEmissiveTriangleIndices = nullptr;
    }
    if (m_dScene != nullptr) {
        cudaFree(m_dScene);
        m_dScene = nullptr;
    }
    m_deviceDirty = true;
}

bool MeshAccelScene::upload(cudaStream_t stream)
{
    if (m_dScene == nullptr || !m_built) {
        return false;
    }

    const bool deviceBuffersReady = m_dBvhNodes != nullptr && m_dTriangles != nullptr &&
        (m_materials.empty() || m_dMaterials != nullptr);
    if (!m_deviceDirty && deviceBuffersReady) {
        return true;
    }

    if (m_dBvhNodes != nullptr) {
        cudaFree(m_dBvhNodes);
        m_dBvhNodes = nullptr;
    }
    if (m_dTriangles != nullptr) {
        cudaFree(m_dTriangles);
        m_dTriangles = nullptr;
    }
    if (m_dMaterials != nullptr) {
        cudaFree(m_dMaterials);
        m_dMaterials = nullptr;
    }
    if (m_dEmissiveTriangleIndices != nullptr) {
        cudaFree(m_dEmissiveTriangleIndices);
        m_dEmissiveTriangleIndices = nullptr;
    }

    const auto uploadBuffer = [stream](auto* devicePtr, const auto& hostData) -> bool {
        return uploadHostVector(devicePtr, hostData, stream);
    };

    if (!uploadBuffer(&m_dBvhNodes, m_bvhNodes)) {
        release();
        return false;
    }
    if (!uploadBuffer(&m_dTriangles, m_triangles)) {
        release();
        return false;
    }
    if (!uploadBuffer(&m_dMaterials, m_materials)) {
        release();
        return false;
    }
    if (!uploadBuffer(&m_dEmissiveTriangleIndices, m_emissiveTriangleIndices)) {
        release();
        return false;
    }

    MeshAccelSceneGpu deviceScene{};
    deviceScene.bvhNodes = m_dBvhNodes;
    deviceScene.triangles = m_dTriangles;
    deviceScene.materials = m_dMaterials;
    deviceScene.emissiveTriangleIndices = m_dEmissiveTriangleIndices;
    deviceScene.bvhNodeCount = static_cast<uint32_t>(m_bvhNodes.size());
    deviceScene.triangleCount = static_cast<uint32_t>(m_triangles.size());
    deviceScene.bvhRootIndex = m_hostScene.bvhRootIndex;
    deviceScene.materialCount = static_cast<uint32_t>(m_materials.size());
    deviceScene.emissiveTriangleCount = static_cast<uint32_t>(m_emissiveTriangleIndices.size());

    if (cudaMemcpyAsync(m_dScene, &deviceScene, sizeof(MeshAccelSceneGpu), cudaMemcpyHostToDevice, stream)
        != cudaSuccess) {
        if (m_dBvhNodes != nullptr) {
            cudaFree(m_dBvhNodes);
            m_dBvhNodes = nullptr;
        }
        if (m_dTriangles != nullptr) {
            cudaFree(m_dTriangles);
            m_dTriangles = nullptr;
        }
        if (m_dMaterials != nullptr) {
            cudaFree(m_dMaterials);
            m_dMaterials = nullptr;
        }
        return false;
    }

    m_deviceDirty = false;
    return true;
}
