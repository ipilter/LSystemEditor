#pragma once

#include "MeshAccel/Mesh.h"

namespace manifold {
class Manifold;
}

Mesh meshFromManifold(const manifold::Manifold& manifoldMesh);
