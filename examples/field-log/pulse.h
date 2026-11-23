#ifndef FIELD_LOG_PULSE_H
#define FIELD_LOG_PULSE_H

#include <nocturne/component.h>

typedef struct Pulse {
    NtCellStyle colors;
    unsigned phase;
} Pulse;

extern const NtComponentOps pulse_ops;

#endif
