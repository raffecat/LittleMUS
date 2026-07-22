// 1-pole LPF (first order, -6 dB/octave)

#include <math.h>

typedef struct {
    float lpf;
    float next;
} LPF_1Pole;

static inline void lpf_1pole_init(LPF_1Pole *f, float sample_rate, float cutoff_hz) {
    f->lpf = 1.0f - expf(-2.0f * M_PI * cutoff_hz / sample_rate);
    f->next = 0.0f;
}

static inline int lpf_1pole_step(LPF_1Pole *f, float samp) {
    f->next += (samp - f->next) * f->lpf;
    return f->next;
}
