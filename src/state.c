#include <nocturne/state.h>
#include <nocturne/text.h>

#include "property_internal.h"

#include <float.h>
#include <string.h>

enum { HEADER_SIZE = 16, ENTRY_SIZE = 13 };
static const unsigned char magic[] = {0x4e,0x54,0x53,0x54};

struct NtStateSnapshot {
    NtAllocator allocator;
    size_t bytes, count;
    unsigned char data[];
};

NT_DECLARE_ALIGNMENT(NtAllocator);
NT_DECLARE_ALIGNMENT(NtPropertyAssignment);

static void store_word(unsigned char *destination, size_t bytes, uint64_t word)
{
    for (size_t i = 0; i < bytes; ++i) {
        destination[i] = (unsigned char)word;
        word >>= 8;
    }
}

static uint64_t load_word(const unsigned char *source, size_t bytes)
{
    uint64_t word = 0;
    while (bytes) word = (word << 8) | source[--bytes];
    return word;
}

static bool binary64(void)
{
    if (sizeof(double) != sizeof(uint64_t) || FLT_RADIX != 2 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024)
        return false;
    const double one = 1.0;
    uint64_t word;
    memcpy(&word, &one, sizeof(word));
    return word == UINT64_C(0x3ff0000000000000);
}

static size_t value_size(NtValue value)
{
    switch (value.kind) {
    case NT_VALUE_BOOL: return 1;
    case NT_VALUE_I64:
    case NT_VALUE_U64:
    case NT_VALUE_REAL: return 8;
    case NT_VALUE_TEXT: return value.as.text.count;
    case NT_VALUE_RGBA: return 4;
    }
    return 0;
}

static void store_value(unsigned char *destination, NtValue value)
{
    switch (value.kind) {
    case NT_VALUE_BOOL: destination[0] = value.as.flag; break;
    case NT_VALUE_I64: store_word(destination, 8, (uint64_t)value.as.i64); break;
    case NT_VALUE_U64: store_word(destination, 8, value.as.u64); break;
    case NT_VALUE_REAL: {
        uint64_t word;
        memcpy(&word, &value.as.real, sizeof(word));
        store_word(destination, 8, word);
        break;
    }
    case NT_VALUE_TEXT:
        if (value.as.text.count) memcpy(destination, value.as.text.data, value.as.text.count);
        break;
    case NT_VALUE_RGBA:
        destination[0] = value.as.rgba.red;
        destination[1] = value.as.rgba.green;
        destination[2] = value.as.rgba.blue;
        destination[3] = value.as.rgba.alpha;
        break;
    }
}

static NtResult read_entry(const unsigned char **cursor, size_t *remaining, NtPropertyId *id, NtValue *out)
{
    if (*remaining < ENTRY_SIZE) return NT_RESULT_INVALID_ARGUMENT;
    const unsigned char *entry = *cursor, *data = entry + ENTRY_SIZE;
    const uint64_t length = load_word(entry + 5, 8);
    *id = (NtPropertyId)load_word(entry, 4);
    if (!*id || !entry[4] || entry[4] > NT_VALUE_RGBA + 1 || length > *remaining - ENTRY_SIZE)
        return NT_RESULT_INVALID_ARGUMENT;
    NtValue value = {.kind=(NtValueKind)(entry[4] - 1)};
    if (value.kind == NT_VALUE_TEXT) {
        value.as.text = (NtByteView){length ? (const char *)data : NULL, (size_t)length};
        const NtResult result = nt_utf8_validate(value.as.text, NULL);
        if (result != NT_RESULT_OK) return result;
    } else {
        if (length != value_size(value)) return NT_RESULT_INVALID_ARGUMENT;
        switch (value.kind) {
        case NT_VALUE_BOOL:
            if (data[0] > 1) return NT_RESULT_INVALID_ARGUMENT;
            value.as.flag = data[0] != 0;
            break;
        case NT_VALUE_I64: {
            const uint64_t word = load_word(data, 8);
            value.as.i64 = word <= INT64_MAX ? (int64_t)word : -1 - (int64_t)(UINT64_MAX - word);
            break;
        }
        case NT_VALUE_U64: value.as.u64 = load_word(data, 8); break;
        case NT_VALUE_REAL: {
            if (!binary64()) return NT_RESULT_UNSUPPORTED_CAPABILITY;
            const uint64_t word = load_word(data, 8);
            if (((word >> 52) & 0x7ff) == 0x7ff) return NT_RESULT_OUT_OF_RANGE;
            memcpy(&value.as.real, &word, sizeof(word));
            break;
        }
        case NT_VALUE_RGBA: value.as.rgba = (NtRgba){data[0],data[1],data[2],data[3]}; break;
        case NT_VALUE_TEXT: break;
        }
    }
    *cursor = data + (size_t)length;
    *remaining -= ENTRY_SIZE + (size_t)length;
    *out = value;
    return NT_RESULT_OK;
}

static NtStateSnapshot *allocate_snapshot(const NtAllocator *allocator, size_t size, size_t count)
{
    NtStateSnapshot *snapshot = nt_allocate(allocator, sizeof(*snapshot) + size, NT_ALIGNMENT(NtAllocator));
    if (snapshot) {
        snapshot->allocator = *allocator;
        snapshot->bytes = size;
        snapshot->count = count;
    }
    return snapshot;
}

void nt_state_snapshot_destroy(NtStateSnapshot *snapshot)
{
    if (snapshot) {
        const NtAllocator allocator = snapshot->allocator;
        nt_release(&allocator, snapshot, sizeof(*snapshot) + snapshot->bytes, NT_ALIGNMENT(NtAllocator));
    }
}

NtByteView nt_state_snapshot_bytes(const NtStateSnapshot *snapshot)
{
    return snapshot ? (NtByteView){(const char *)snapshot->data, snapshot->bytes} : (NtByteView){0};
}

NtResult nt_state_snapshot_import(const NtAllocator *allocator, NtByteView bytes, NtStateSnapshot **out)
{
    NtAllocator selected;
    if (!out || !nt_allocator_get(allocator, &selected) || !bytes.data || bytes.count < HEADER_SIZE)
        return NT_RESULT_INVALID_ARGUMENT;
    if (bytes.count > SIZE_MAX - sizeof(NtStateSnapshot)) return NT_RESULT_OUT_OF_RANGE;
    const unsigned char *data = (const unsigned char *)bytes.data;
    if (memcmp(data, magic, sizeof(magic))) return NT_RESULT_INVALID_ARGUMENT;
    if (load_word(data + 4, 4) != NT_STATE_SCHEMA_VERSION) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    const uint64_t count = load_word(data + 8, 8);
    size_t remaining = bytes.count - HEADER_SIZE;
    if (count > remaining / ENTRY_SIZE) return NT_RESULT_INVALID_ARGUMENT;
    const unsigned char *cursor = data + HEADER_SIZE;
    for (size_t i = 0; i < (size_t)count; ++i) {
        NtPropertyId id;
        NtValue value;
        const NtResult result = read_entry(&cursor, &remaining, &id, &value);
        if (result != NT_RESULT_OK) return result;
    }
    if (remaining) return NT_RESULT_INVALID_ARGUMENT;
    NtStateSnapshot *snapshot = allocate_snapshot(&selected, bytes.count, (size_t)count);
    if (!snapshot) return NT_RESULT_ALLOCATION_FAILED;
    memcpy(snapshot->data, data, bytes.count);
    *out = snapshot;
    return NT_RESULT_OK;
}

static NtResult capture_entries(NtTree *tree, const NtPropertyRef *properties, size_t count,
                                 const NtAllocator *allocator, NtStateSnapshot **out)
{
    const size_t bytes = count * sizeof(NtPropertyAssignment);
    NtPropertyAssignment *entries = bytes ? nt_allocate(allocator, bytes, NT_ALIGNMENT(NtPropertyAssignment)) : NULL;
    if (bytes && !entries) return NT_RESULT_ALLOCATION_FAILED;
    NtResult result = NT_RESULT_OK;
    size_t copied = 0, total = HEADER_SIZE;
    while (copied < count) {
        NtAttachedProperty property;
        NtValue value;
        result = nt_property_lookup(tree, properties[copied], true, &property);
        if (result == NT_RESULT_OK) result = nt_property_read(property, &value);
        if (result != NT_RESULT_OK) break;
        if (value.kind == NT_VALUE_REAL && !binary64()) {
            result = NT_RESULT_UNSUPPORTED_CAPABILITY;
            break;
        }
        const size_t length = value_size(value);
        const size_t capacity = SIZE_MAX - sizeof(NtStateSnapshot) - total;
        if (capacity < ENTRY_SIZE || length > capacity - ENTRY_SIZE) {
            result = NT_RESULT_OUT_OF_RANGE;
            break;
        }
        entries[copied].property = properties[copied];
        result = nt_value_copy(allocator, value, &entries[copied].value);
        if (result != NT_RESULT_OK) break;
        total += ENTRY_SIZE + length;
        ++copied;
    }
    if (result == NT_RESULT_OK) {
        NtStateSnapshot *snapshot = allocate_snapshot(allocator, total, count);
        if (!snapshot) result = NT_RESULT_ALLOCATION_FAILED;
        else {
            memcpy(snapshot->data, magic, sizeof(magic));
            store_word(snapshot->data + 4, 4, NT_STATE_SCHEMA_VERSION);
            store_word(snapshot->data + 8, 8, count);
            unsigned char *cursor = snapshot->data + HEADER_SIZE;
            for (size_t i = 0; i < count; ++i) {
                const size_t length = value_size(entries[i].value);
                store_word(cursor, 4, entries[i].property.id);
                cursor[4] = (unsigned char)(entries[i].value.kind + 1);
                store_word(cursor + 5, 8, length);
                store_value(cursor + ENTRY_SIZE, entries[i].value);
                cursor += ENTRY_SIZE + length;
            }
            *out = snapshot;
        }
    }
    for (size_t i = 0; i < copied; ++i) nt_value_release(allocator, &entries[i].value);
    nt_release(allocator, entries, bytes, NT_ALIGNMENT(NtPropertyAssignment));
    return result;
}

NtResult nt_tree_state_capture(NtTree *tree, const NtPropertyRef *properties, size_t count,
                                const NtAllocator *allocator, NtStateSnapshot **out)
{
    NtAllocator selected;
    if (!tree || !out || (count && !properties) || !nt_allocator_get(allocator, &selected))
        return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (count > SIZE_MAX / sizeof(NtPropertyAssignment) || count > SIZE_MAX / sizeof(*properties))
        return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < count; ++i) {
        NtAttachedProperty property;
        const NtResult result = nt_property_lookup(tree, properties[i], true, &property);
        if (result != NT_RESULT_OK) return result;
        for (size_t j = 0; j < i; ++j)
            if (properties[j].id == properties[i].id && nt_node_same(properties[j].node, properties[i].node))
                return NT_RESULT_INVALID_ARGUMENT;
    }
    tree->busy = true;
    const NtResult result = capture_entries(tree, properties, count, &selected, out);
    tree->busy = false;
    return result;
}

NtResult nt_tree_state_restore(NtTree *tree, const NtPropertyRef *properties, size_t count,
                                const NtStateSnapshot *snapshot)
{
    if (!tree || !snapshot || count != snapshot->count || (count && !properties))
        return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (!count) return NT_RESULT_OK;
    if (count > SIZE_MAX / sizeof(NtPropertyAssignment) || count > SIZE_MAX / sizeof(*properties))
        return NT_RESULT_OUT_OF_RANGE;
    tree->busy = true;
    const size_t bytes = count * sizeof(NtPropertyAssignment);
    NtPropertyAssignment *entries = nt_allocate(&tree->allocator, bytes, NT_ALIGNMENT(NtPropertyAssignment));
    NtResult result = entries ? NT_RESULT_OK : NT_RESULT_ALLOCATION_FAILED;
    const unsigned char *cursor = snapshot->data + HEADER_SIZE;
    size_t remaining = snapshot->bytes - HEADER_SIZE;
    for (size_t i = 0; i < count && result == NT_RESULT_OK; ++i) {
        NtPropertyId id;
        entries[i].property = properties[i];
        result = read_entry(&cursor, &remaining, &id, &entries[i].value);
        if (result == NT_RESULT_OK && id != properties[i].id) result = NT_RESULT_INVALID_ARGUMENT;
    }
    if (result == NT_RESULT_OK) result = nt_property_apply_batch(tree, entries, count);
    nt_release(&tree->allocator, entries, bytes, NT_ALIGNMENT(NtPropertyAssignment));
    tree->busy = false;
    return result;
}
