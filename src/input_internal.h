#ifndef NOCTURNE_INTERNAL_INPUT_H
#define NOCTURNE_INTERNAL_INPUT_H

#include <nocturne/input.h>

enum { NT_INPUT_SEQUENCE_LIMIT = 64, NT_INPUT_TEXT_LIMIT = 64 };

typedef enum NtInputMode {
    NT_INPUT_GROUND, NT_INPUT_ESCAPE, NT_INPUT_UTF8, NT_INPUT_CSI, NT_INPUT_SS3,
    NT_INPUT_STRING, NT_INPUT_PASTE
} NtInputMode;

struct NtInputDecoder {
    NtAllocator allocator;
    NtInputOptions options;
    NtTimeUs last_time, escape_at, escape_due;
    NtInputMode mode;
    char sequence[NT_INPUT_SEQUENCE_LIMIT], text[NT_INPUT_TEXT_LIMIT];
    size_t count, utf8_size;
    bool alt, discard, string_escape, osc, cursor_report;
    char *paste;
    size_t paste_count, paste_capacity, paste_match;
    uint32_t pressed;
};

bool nt_input_printable(uint32_t scalar);
void nt_input_key(NtInputRead *out, NtTimeUs now, uint32_t key, uint32_t mods, NtKeyState state);
void nt_input_scalar(NtInputDecoder *decoder, NtInputRead *out, NtTimeUs now, uint32_t scalar);
void nt_input_sequence(NtInputDecoder *decoder, NtInputRead *out, NtTimeUs now,
                       NtInputMode mode, unsigned char final);

#endif
