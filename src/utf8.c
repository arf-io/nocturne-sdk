#include <nocturne/text.h>

NtResult nt_utf8_decode(NtByteView input, size_t *offset, uint32_t *scalar)
{
    if (!offset || !scalar || (!input.data && input.count))
        return NT_RESULT_INVALID_ARGUMENT;
    const size_t begin = *offset;
    if (begin >= input.count) return NT_RESULT_OUT_OF_RANGE;

    const unsigned char *bytes = (const unsigned char *)input.data + begin;
    const unsigned char lead = bytes[0];
    size_t length;
    uint32_t value, minimum;
    if (lead <= 0x7f) {
        length = 1;
        value = lead;
        minimum = 0;
    } else if (lead >= 0xc2 && lead <= 0xdf) {
        length = 2;
        value = lead & 0x1f;
        minimum = 0x80;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        length = 3;
        value = lead & 0x0f;
        minimum = 0x800;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        length = 4;
        value = lead & 0x07;
        minimum = 0x10000;
    } else {
        return NT_RESULT_INVALID_ARGUMENT;
    }
    if (length > input.count - begin) return NT_RESULT_INVALID_ARGUMENT;
    for (size_t i = 1; i < length; ++i) {
        const unsigned char byte = bytes[i];
        if ((byte & 0xc0) != 0x80) return NT_RESULT_INVALID_ARGUMENT;
        value = (value << 6) | (byte & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        return NT_RESULT_INVALID_ARGUMENT;

    *offset = begin + length;
    *scalar = value;
    return NT_RESULT_OK;
}

NtResult nt_utf8_validate(NtByteView input, size_t *scalar_count)
{
    if (!input.data && input.count) return NT_RESULT_INVALID_ARGUMENT;
    size_t offset = 0, count = 0;
    while (offset < input.count) {
        uint32_t scalar;
        const NtResult result = nt_utf8_decode(input, &offset, &scalar);
        if (result != NT_RESULT_OK) return result;
        ++count;
    }
    if (scalar_count) *scalar_count = count;
    return NT_RESULT_OK;
}
