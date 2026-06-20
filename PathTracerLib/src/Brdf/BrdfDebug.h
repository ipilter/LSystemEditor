#pragma once

#include <cstdint>

/** @brief Bit flags for isolating glass/transmission paths during debugging. */
struct BrdfDebugFlags
{
    static constexpr int kNone = 0;
    /** @brief Skip diffuse/specular/subsurface lobes; always sample transmit. */
    static constexpr int kForceTransmitLobeOnly = 1 << 0;
    /** @brief In sampleTransmit, always mirror-reflect (never refract). */
    static constexpr int kDisableRefract = 1 << 1;
    /** @brief In sampleTransmit, always refract/thin-flip (never Fresnel reflect). */
    static constexpr int kDisableReflect = 1 << 2;
    /** @brief On TIR, return invalid sample instead of mirror-reflect fallback. */
    static constexpr int kDisableTirFallback = 1 << 3;
    /** @brief Tint transmitted paths green and reflected paths red in the integrator. */
    static constexpr int kTintGlassPaths = 1 << 4;
};
