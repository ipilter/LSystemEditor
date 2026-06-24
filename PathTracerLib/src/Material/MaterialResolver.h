#pragma once

#include "Material/MaterialChannels.h"
#include "MeshAccel/MeshAccelTypes.h"
#include "Texture/ProceduralTexture.h"

/**
 * Host-side material resolution entry points. Texture evaluation lives in
 * ProceduralTexture.h; this header documents the resolver surface for the
 * modular material system.
 */
using MaterialResolverContext = TextureEvalContext;
