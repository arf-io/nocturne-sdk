#include <nocturne/motion.h>
#include <math.h>

static double segment_length(NtPoint a, NtPoint b) {
    return hypot(b.x - a.x, b.y - a.y);
}

NtStatus nt_path_length(const NtPath *path, double *out) {
    if (!path || !out || !path->points || (path->closed != 0 && path->closed != 1) ||
        path->count < (path->closed ? 3u : 2u) ||
        path->boundary < NT_ONCE || path->boundary > NT_PING_PONG) return NT_INVALID_ARGUMENT;
    for (size_t i = 0; i < path->count; ++i) {
        if (!isfinite(path->points[i].x) || !isfinite(path->points[i].y)) return NT_INVALID_ARGUMENT;
    }
    size_t segments = path->closed ? path->count : path->count - 1;
    double total = 0;
    for (size_t i = 0; i < segments; ++i) {
        NtPoint a = path->points[i], b = path->points[(i + 1) % path->count];
        if (a.x == b.x && a.y == b.y) return NT_INVALID_ARGUMENT;
        double length = segment_length(a, b);
        if (!isfinite(length) || length == 0 || !isfinite(total + length) ||
            total + length == total) return NT_OUT_OF_RANGE;
        total += length;
    }
    if (path->boundary == NT_PING_PONG && !isfinite(total * 2)) return NT_OUT_OF_RANGE;
    *out = total;
    return NT_OK;
}

NtStatus nt_path_sample(const NtPath *path, double distance, NtPathSample *out) {
    if (!out || !isfinite(distance)) return NT_INVALID_ARGUMENT;
    double total;
    NtStatus status = nt_path_length(path, &total);
    if (status != NT_OK) return status;
    double at = distance, direction = 1;
    if (path->boundary == NT_ONCE) at = fmax(0, fmin(total, at));
    else {
        double period = path->boundary == NT_PING_PONG ? total * 2 : total;
        at = fmod(at, period);
        if (at < 0) at += period;
        if (at >= period) at = 0;
        if (path->boundary == NT_PING_PONG && at >= total) {
            at = period - at;
            direction = -1;
        }
    }
    double local = at;
    size_t segments = path->closed ? path->count : path->count - 1;
    for (size_t i = 0; i < segments; ++i) {
        NtPoint a = path->points[i], b = path->points[(i + 1) % path->count];
        double length = segment_length(a, b);
        if (local < length || (direction < 0 && local == length) || i == segments - 1) {
            double t = at == total ? 1 : fmax(0, fmin(1, local / length));
            NtPathSample sample;
            sample.position.x = t == 0 ? a.x : t == 1 ? b.x : a.x + (b.x - a.x) * t;
            sample.position.y = t == 0 ? a.y : t == 1 ? b.y : a.y + (b.y - a.y) * t;
            sample.tangent.x = direction * ((b.x - a.x) / length);
            sample.tangent.y = direction * ((b.y - a.y) / length);
            sample.distance = at;
            sample.length = total;
            if (!isfinite(sample.position.x) || !isfinite(sample.position.y)) return NT_OUT_OF_RANGE;
            *out = sample;
            return NT_OK;
        }
        local -= length;
    }
    return NT_OUT_OF_RANGE;
}
