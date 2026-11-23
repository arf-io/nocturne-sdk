#ifndef NOCTURNE_INTERNAL_ANIMATION_H
#define NOCTURNE_INTERNAL_ANIMATION_H

#include <nocturne/animation.h>

static inline double nt_animation_mix(double a, double b, double t)
{
    if (t == 0) return a;
    if (t == 1) return b;
    /* Avoid overflowing b-a when finite endpoints have opposite signs. */
    return (a < 0) != (b < 0) ? (1 - t) * a + t * b : a + t * (b - a);
}

double nt_animation_progress(uint64_t at, uint64_t duration);
uint64_t nt_animation_timing(uint64_t elapsed, uint64_t delay, uint64_t duration,
                             NtRepeat repeat, NtAnimationSample *sample);

#endif
