#ifndef NOCTURNE_BASE_H
#define NOCTURNE_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Common value types and result categories. Individual APIs define their errors. */
typedef enum NtResult {
    NT_RESULT_OK = 0,
    NT_RESULT_INVALID_ARGUMENT = 1,
    NT_RESULT_OUT_OF_RANGE = 2,
    NT_RESULT_ALLOCATION_FAILED = 3,
    NT_RESULT_INSUFFICIENT_BUFFER = 4,
    NT_RESULT_STALE_REFERENCE = 5,
    NT_RESULT_WRONG_PHASE = 6,
    NT_RESULT_UNSUPPORTED_CAPABILITY = 7,
    NT_RESULT_TRANSPORT_FAILED = 8,
    NT_RESULT_CALLBACK_FAILED = 9
} NtResult;

typedef uint64_t NtTimeUs;
/* Encoded sRGB channels and straight alpha, in byte order RGBA. */
typedef struct NtRgba { uint8_t red, green, blue, alpha; } NtRgba;

typedef struct NtByteView {
    const char *data;
    size_t count;
} NtByteView;

/* For APIs that accept an allocator, NULL selects the C allocator. A custom
 * allocator supplies both callbacks. The object copies this record and its
 * context must remain valid until destruction. Callbacks receive nonzero
 * sizes and the required alignment in bytes. Release receives the exact size
 * and alignment of the original successful allocation. Return NULL on
 * allocation failure. Callbacks must not reenter the object being mutated. */
typedef struct NtAllocator {
    void *context;
    void *(*allocate)(void *context, size_t bytes, size_t alignment);
    void (*release)(void *context, void *address, size_t bytes, size_t alignment);
} NtAllocator;

typedef struct NtCellPoint { int32_t x, y; } NtCellPoint;
typedef struct NtCellSize { uint32_t columns, rows; } NtCellSize;
typedef struct NtCellRect { NtCellPoint origin; NtCellSize size; } NtCellRect;
typedef struct NtExtentRange { uint32_t min, max; bool bounded; } NtExtentRange;
typedef struct NtConstraints { NtExtentRange columns, rows; } NtConstraints;

typedef enum NtWidthChoice { NT_WIDTH_ONE = 1, NT_WIDTH_TWO = 2 } NtWidthChoice;
typedef struct NtWidthPolicy { NtWidthChoice ambiguous, emoji; } NtWidthPolicy;
typedef enum NtMotionPolicy { NT_MOTION_FULL, NT_MOTION_REDUCED } NtMotionPolicy;

#ifdef __cplusplus
}
#endif
#endif
