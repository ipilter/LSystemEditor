#include "MeshAccelGltfIO.h"
#include "MeshAccelScene.h"

bool MeshAccelScene::exportGltf(const QString& glbFilePath, QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (!m_built) {
        return fail(QStringLiteral("Scene has not been built."));
    }

    if (m_triangles.empty()) {
        return fail(QStringLiteral("Scene has no geometry to export."));
    }

    return exportMeshGltf(m_triangles, m_materials, m_textures, glbFilePath, errorMessage);
}
