#pragma once

#include "HostMesh.h"

namespace manifold {
class Manifold;
}

HostMesh meshFromManifold(const manifold::Manifold& manifoldMesh);
