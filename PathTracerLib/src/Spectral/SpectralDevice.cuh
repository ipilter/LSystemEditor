#pragma once

#include "Rgb2Spec.h"

#if defined(__CUDACC__)

__device__ Rgb2SpecGpu spectralActiveModel();

#endif
