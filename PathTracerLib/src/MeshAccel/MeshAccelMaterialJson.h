#pragma once

#include "MeshAccel/MeshAccelTypes.h"

#include <QJsonObject>
#include <QString>
#include <vector>

/** @brief Writes PathTracer materials and procedural texture bank to JSON sidecar. */
bool writeMaterialsJsonSidecar(
    const QString& jsonFilePath,
    const std::vector<MaterialGpu>& materials,
    const std::vector<TextureDescGpu>& textures,
    QString* errorMessage = nullptr);

/** @brief Reads materials and textures from a PathTracer materials JSON sidecar. */
bool readMaterialsJsonSidecar(
    const QString& jsonFilePath,
    std::vector<MaterialGpu>* outMaterials,
    std::vector<TextureDescGpu>* outTextures,
    QString* errorMessage = nullptr);

/** @brief Builds a QJsonObject for glTF material extras.pathTracer block. */
QJsonObject pathTracerMaterialExtrasJson(const MaterialGpu& material);

/** @brief Populates PathTracer-specific fields from glTF extras.pathTracer. */
void applyPathTracerMaterialExtrasJson(const QJsonObject& extras, MaterialGpu* material);
