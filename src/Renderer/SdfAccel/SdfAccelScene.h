#pragma once

#include "SdfAccelField.h"
#include "SdfAccelTypes.h"

#include <cuda_runtime.h>
#include <vector>

class SdfAccelScene
{
public:
    SdfAccelScene();
    ~SdfAccelScene();

    SdfAccelScene(const SdfAccelScene&) = delete;
    SdfAccelScene& operator=(const SdfAccelScene&) = delete;

    void clear();
    void setBuildParams(const SdfAccelBuildParams& params);
    void setDefaultLayout();

    void addSphere(SdfFloat3 center, float radius);
    void addCylinder(SdfFloat3 center, SdfFloat2 halfExtents);
    void addCappedCone(SdfFloat3 center, float halfHeight, float radiusBottom, float radiusTop);

    bool build();

    bool allocate();
    void release();
    bool upload(cudaStream_t stream);

    const SdfAccelSceneGpu* deviceScene() const { return m_dScene; }
    const SdfAccelSceneGpu* hostScene() const { return m_built ? &m_hostScene : nullptr; }

    float evalSDF(SdfFloat3 p) const;
    SdfHit rayMarch(SdfFloat3 ro, SdfFloat3 rd, const SdfMarchParamsGpu& params) const;

private:
    struct PendingObject
    {
        SdfAccelField field;
    };

    SdfAccelBuildParams m_buildParams{};
    std::vector<PendingObject> m_pendingObjects;

    std::vector<SdfOctreeNode> m_octreeNodes;
    std::vector<SdfBvhNode> m_bvhNodes;
    std::vector<SdfAccelObjectGpu> m_objects;
    std::vector<SdfAccelPayloadGpu> m_payloads;
    SdfAccelSceneGpu m_hostScene{};
    bool m_built = false;

    SdfAccelSceneGpu* m_dScene = nullptr;
    SdfBvhNode* m_dBvhNodes = nullptr;
    SdfOctreeNode* m_dOctreeNodes = nullptr;
    SdfAccelObjectGpu* m_dObjects = nullptr;
    SdfAccelPayloadGpu* m_dPayloads = nullptr;

    bool m_deviceDirty = true;
};
