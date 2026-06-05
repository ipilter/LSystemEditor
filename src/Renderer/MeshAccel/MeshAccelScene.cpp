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
    gpu.materialIndex = tri.materialIndex;

    const Vec3 e1 = vecSub3(tri.v1, tri.v0);
    const Vec3 e2 = vecSub3(tri.v2, tri.v0);
    const Vec3 cross = vecMake3(
        e1.y * e2.z - e1.z * e2.y,
        e1.z * e2.x - e1.x * e2.z,
        e1.x * e2.y - e1.y * e2.x);
    gpu.normal = vecNormalize3(cross);
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
    return material;
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

    m_hostScene = MeshAccelSceneGpu{};
    m_hostScene.bvhNodes = m_bvhNodes.data();
    m_hostScene.triangles = m_triangles.data();
    m_hostScene.materials = m_materials.data();
    m_hostScene.bvhNodeCount = static_cast<uint32_t>(m_bvhNodes.size());
    m_hostScene.triangleCount = static_cast<uint32_t>(m_triangles.size());
    m_hostScene.bvhRootIndex = bvhRootIndex;
    m_hostScene.materialCount = static_cast<uint32_t>(m_materials.size());

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

    if (!m_deviceDirty && m_dBvhNodes != nullptr && m_dTriangles != nullptr) {
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
    if (!allocAndCopy(&m_dTriangles, m_triangles)) {
        release();
        return false;
    }
    if (!allocAndCopy(&m_dMaterials, m_materials)) {
        release();
        return false;
    }

    MeshAccelSceneGpu deviceScene{};
    deviceScene.bvhNodes = m_dBvhNodes;
    deviceScene.triangles = m_dTriangles;
    deviceScene.materials = m_dMaterials;
    deviceScene.bvhNodeCount = static_cast<uint32_t>(m_bvhNodes.size());
    deviceScene.triangleCount = static_cast<uint32_t>(m_triangles.size());
    deviceScene.bvhRootIndex = m_hostScene.bvhRootIndex;
    deviceScene.materialCount = static_cast<uint32_t>(m_materials.size());

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
