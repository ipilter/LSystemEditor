#pragma once

#include <cstdint>

/// Joe–Kuo Sobol direction numbers (nlopt soboldata.h, MIT license).
constexpr int kSobolBits = 32;
constexpr int kMaxSobolDimensions = 1024;

/// Fills `out` with `maxDimensions * kSobolBits` direction numbers (bit-major layout:
/// index `bit * maxDimensions + dim`, matching nlopt `m[bit][dim]`).
bool buildSobolMatricesHost(uint32_t* out, int maxDimensions);
