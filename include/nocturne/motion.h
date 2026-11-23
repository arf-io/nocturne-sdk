#ifndef NOCTURNE_MOTION_H
#define NOCTURNE_MOTION_H

#include "animation.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { double x, y; } NtPoint;
typedef struct {
    const NtPoint *points;
    size_t count;
    int closed;
    NtRepeat boundary;
} NtPath;
typedef struct {
    NtPoint position, tangent;
    double distance, length;
} NtPathSample;

/* Piecewise-linear distance in caller-defined coordinates. Screen conventions
 * are x right, y down; the sampler itself is coordinate-system independent.
 * Open paths need >=2 points, closed paths >=3. Adjacent points must differ,
 * including the closing segment. Every coordinate and distance must be finite.
 * Geometry whose differences/length/period cannot fit double returns RANGE.
 * ONCE clamps; LOOP wraps; PING_PONG reverses (including negative distance).
 * At a join, the next segment in the resolved travel direction wins; at the
 * final open endpoint, the final segment wins. Tangent follows increasing requested distance, including the
 * reflected direction. A caller traversing with decreasing distance reverses it.
 * Outputs stay untouched on error. No allocation or hidden lifetime. */
NtStatus nt_path_length(const NtPath *path, double *out);
NtStatus nt_path_sample(const NtPath *path, double distance, NtPathSample *out);

#ifdef __cplusplus
}
#endif
#endif
