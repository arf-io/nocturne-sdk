#ifndef NOCTURNE_INPUT_H
#define NOCTURNE_INPUT_H

#include <nocturne/event.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtInputDecoder NtInputDecoder;

typedef enum NtKeyboardProtocol { NT_KEYBOARD_XTERM, NT_KEYBOARD_KITTY } NtKeyboardProtocol;
typedef struct NtInputOptions {
    NtTimeUs escape_delay;      /* Lone Escape disambiguation, in microseconds. */
    size_t paste_limit;         /* Maximum payload bytes. Zero permits empty paste. */
    NtKeyboardProtocol keyboard;
} NtInputOptions;

typedef enum NtInputStatus { NT_INPUT_PENDING, NT_INPUT_READY, NT_INPUT_DISCARDED } NtInputStatus;
typedef struct NtInputRead {
    size_t consumed;
    NtInputStatus status;
    NtEvent event;             /* Present on READY. */
    NtByteView key_text;        /* Optional separate NT_EVENT_TEXT for this key. */
} NtInputRead;

/* NULL options select a 25 ms Escape delay and a 1 MiB paste limit.
 * The caller serializes access and owns the transport and clock. */
NtResult nt_input_create(const NtAllocator *allocator, const NtInputOptions *options,
                          NtInputDecoder **out);
void nt_input_destroy(NtInputDecoder *decoder);
/* Read at most one record. Resume at bytes + consumed, including when a
 * pending Escape or a discarded prefix produces a record with consumed zero.
 * TEXT and PASTE and key_text borrow storage until the next mutation.
 * A zero-byte read can deliver an expired lone Escape. Timestamps supplied
 * to read and cancel are nondecreasing. Invalid calls leave output intact.
 * Allocation failure returns the accepted prefix and leaves the next byte
 * pending for retry. Input must not alias decoder-owned storage. */
NtResult nt_input_read(NtInputDecoder *decoder, NtByteView bytes, NtTimeUs now, NtInputRead *out);
bool nt_input_deadline(const NtInputDecoder *decoder, NtTimeUs *out);
/* Give the next CSI cursor-position reply precedence over XTerm F3.
 * Set before issuing a query. Clear on a caller-owned query timeout. */
NtResult nt_input_expect_cursor_report(NtInputDecoder *decoder, bool expected);
/* Clear buffered input, pressed buttons and any cursor reply expectation,
 * then return one host cancellation. */
NtResult nt_input_cancel(NtInputDecoder *decoder, NtTimeUs now, NtCancelReason reason, NtEvent *out);

#ifdef __cplusplus
}
#endif
#endif
