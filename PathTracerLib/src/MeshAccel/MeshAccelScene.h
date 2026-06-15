#pragma once

#include "MeshAccelTypes.h"
#include "MeshBuilder/HostMesh.h"

#include <QString>

#include <cuda_runtime.h>
#include <vector>

class MeshAccelScene
{
public:
    MeshAccelScene();
    ~MeshAccelScene();

    MeshAccelScene(const MeshAccelScene&) = delete;
    MeshAccelScene& operator=(const MeshAccelScene&) = delete;

    void clear();

    bool build(const HostMesh& mesh);

    bool allocate();
    void release();
    bool upload(cudaStream_t stream);

    const MeshAccelSceneGpu* deviceScene() const { return m_dScene; }
    const MeshAccelSceneGpu* hostScene() const { return m_built ? &m_hostScene : nullptr; }

    bool isBuilt() const { return m_built; }
    const std::vector<TriangleGpu>& trianglesHost() const { return m_triangles; }
    const std::vector<MeshBvhNode>& bvhNodesHost() const { return m_bvhNodes; }
    const std::vector<MaterialGpu>& materialsHost() const { return m_materials; }
    const std::vector<uint32_t>& emissiveTriangleIndicesHost() const { return m_emissiveTriangleIndices; }

    /** @brief Writes Wavefront OBJ + sibling MTL from cached host geometry and materials. */
    bool exportWavefrontObj(const QString& objFilePath, QString* errorMessage = nullptr) const;

private:
    std::vector<MeshBvhNode> m_bvhNodes;
    std::vector<TriangleGpu> m_triangles;
    std::vector<MaterialGpu> m_materials;
    std::vector<uint32_t> m_emissiveTriangleIndices;
    MeshAccelSceneGpu m_hostScene{};
    bool m_built = false;

    MeshAccelSceneGpu* m_dScene = nullptr;
    MeshBvhNode* m_dBvhNodes = nullptr;
    TriangleGpu* m_dTriangles = nullptr;
    MaterialGpu* m_dMaterials = nullptr;
    uint32_t* m_dEmissiveTriangleIndices = nullptr;

    bool m_deviceDirty = true;
};
