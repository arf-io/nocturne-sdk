#ifndef NOCTURNE_GRAPHEME_INTERNAL_H
#define NOCTURNE_GRAPHEME_INTERNAL_H

#include <nocturne/base.h>

/* Both slices contain valid UTF-8. The left slice is a single grapheme
 * or an ASCII cell fallback. Only the first scalar of the right is read. */
bool nt_grapheme_separates(NtByteView left, NtByteView right);

#endif
