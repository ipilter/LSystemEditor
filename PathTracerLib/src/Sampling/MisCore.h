#pragma once

#include <cmath>

#if defined(__CUDACC__)
#define MIS_CORE_FN __host__ __device__ inline
#else
#define MIS_CORE_FN inline
#endif

MIS_CORE_FN float misBalanceWeight(float pdfA, float pdfB)
{
    if (pdfA <= 0.0f) {
        return 0.0f;
    }
    const float denom = pdfA + pdfB;
    if (denom <= 0.0f) {
        return 0.0f;
    }
    return pdfA / denom;
}

MIS_CORE_FN float misPowerWeight(float pdfA, float pdfB, float beta)
{
    if (pdfA <= 0.0f) {
        return 0.0f;
    }
    const float a = powf(pdfA, beta);
    const float b = powf(pdfB, beta);
    const float denom = a + b;
    if (denom <= 0.0f) {
        return 0.0f;
    }
    return a / denom;
}

#undef MIS_CORE_FN
