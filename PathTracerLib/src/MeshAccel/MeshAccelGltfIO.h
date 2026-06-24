#pragma once

#include "MeshAccel/Mesh.h"

#include <QString>

/** @brief Exports cached scene geometry and materials as glTF 2.0 binary (.glb). Positions are converted mm→m. */
bool exportMeshGltf(
    const std::vector<TriangleGpu>& triangles,
    const std::vector<MaterialGpu>& materials,
    const std::vector<TextureDescGpu>& textures,
    const QString& glbFilePath,
    QString* errorMessage = nullptr);

/** @brief Imports glTF 2.0 (.glb or .gltf+.bin). Positions are converted m→mm. */
bool importMeshGltf(
    const QString& gltfFilePath,
    Mesh* outMesh,
    QString* errorMessage = nullptr);
