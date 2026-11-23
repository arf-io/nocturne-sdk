#ifndef NOCTURNE_TEXT_INTERNAL_H
#define NOCTURNE_TEXT_INTERNAL_H

#include <nocturne/base.h>

static inline bool nt_text_hard_break(uint32_t scalar)
{
    return (scalar >= 0x0a && scalar <= 0x0d) || scalar == 0x85 ||
           scalar == 0x2028 || scalar == 0x2029;
}

#endif
