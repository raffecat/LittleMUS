// Butterworth 2-Pole (2nd‑order) Biquad LPF (Q = ~0.707)
// Exact −3 dB at cutoff; −12 dB/oct slope.
// Direct Form II Transposed (DF2T) with z1, z2 states.

#pragma once
#ifndef __LPF_BIQUAD__
#define __LPF_BIQUAD__

#include <math.h>

typedef struct {
    float b0, b1;  // feedforward
    float a1, a2;  // feedback (a0 normalized to 1)
    float z1, z2;  // state
} LPF_Biquad;

static inline void lpf_biquad_init(LPF_Biquad *b, float sample_rate, float cutoff_hz, float Q) {
    float w0    = 2.0f * (float)M_PI * cutoff_hz / sample_rate;
    float cw    = cosf(w0);
    float sw    = sinf(w0);
    float alpha = sw / (2.0f * Q);

    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cw;
    float a2 = 1.0f - alpha;
    float b0 = (1.0f - cw) * 0.5f;
    float b1 = 1.0f - cw;

    // normalize
    b->b0 = b0 / a0;
    b->b1 = b1 / a0;
    b->a1 = a1 / a0;
    b->a2 = a2 / a0;

    b->z1 = b->z2 = 0.0f;
}

static inline int lpf_biquad_step(LPF_Biquad *b, float x) {
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b0 * x - b->a2 * y;
    return (int)y; // quantize
}

#endif
