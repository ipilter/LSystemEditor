#pragma once

#include "Brdf/BrdfBase.h"

/** @brief RGB path throughput state (v1). Spectral hero sampling can plug in here later. */
struct PathState
{
    Vec3 throughput = vecMake3(1.0f, 1.0f, 1.0f);
};

using PathSpectralState = PathState;
