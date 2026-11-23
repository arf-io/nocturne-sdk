#ifndef NOCTURNE_PRESENT_INTERNAL_H
#define NOCTURNE_PRESENT_INTERNAL_H

#include <nocturne/present.h>

NtResult nt_ansi_present_in_place(const NtAnsiPresenter *presenter, const NtFrame *frame);

static inline bool nt_ansi_capabilities_valid(NtTerminalCapabilities capabilities,
                                               NtPaletteView palette)
{
    if (capabilities.color < NT_COLOR_MONO || capabilities.color > NT_COLOR_TRUECOLOR ||
        (capabilities.width.ambiguous != NT_WIDTH_ONE && capabilities.width.ambiguous != NT_WIDTH_TWO) ||
        (capabilities.width.emoji != NT_WIDTH_ONE && capabilities.width.emoji != NT_WIDTH_TWO))
        return false;
    if (capabilities.interactive && capabilities.color == NT_COLOR_INDEXED) {
        if (!palette.colors || !palette.count || palette.count > 256) return false;
        for (size_t i = 0; i < palette.count; ++i)
            if (palette.colors[i].alpha != 255) return false;
    }
    return true;
}

#endif
