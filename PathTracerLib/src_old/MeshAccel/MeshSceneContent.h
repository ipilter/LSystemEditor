#pragma once

#include "MeshAccel/MeshAccelScene.h"
#include "MeshAccel/Mesh.h"

#include <QString>

bool meshSceneBuild(const Mesh& mesh, MeshAccelScene& scene, QString* outError = nullptr);
