#include "input_internal.h"

#include "allocator.h"
#include <nocturne/text.h>

#include <string.h>

NT_DECLARE_ALIGNMENT(NtInputDecoder);

enum { ESC = 0x1b };

NtResult nt_input_create(const NtAllocator *allocator, const NtInputOptions *options,
                          NtInputDecoder **out)
{
    NtAllocator memory;
    if (!out || !nt_allocator_get(allocator, &memory) ||
        (options && options->keyboard != NT_KEYBOARD_XTERM && options->keyboard != NT_KEYBOARD_KITTY))
        return NT_RESULT_INVALID_ARGUMENT;
    NtInputDecoder *decoder = nt_allocate(&memory, sizeof(*decoder), NT_ALIGNMENT(NtInputDecoder));
    if (!decoder) return NT_RESULT_ALLOCATION_FAILED;
    *decoder = (NtInputDecoder){.allocator=memory,
        .options=options ? *options : (NtInputOptions){25000,1024u * 1024u,NT_KEYBOARD_XTERM}};
    *out = decoder;
    return NT_RESULT_OK;
}

void nt_input_destroy(NtInputDecoder *decoder)
{
    if (!decoder) return;
    const NtAllocator allocator = decoder->allocator;
    nt_release(&allocator, decoder->paste, decoder->paste_capacity, 1);
    nt_release(&allocator, decoder, sizeof(*decoder), NT_ALIGNMENT(NtInputDecoder));
}

bool nt_input_deadline(const NtInputDecoder *decoder, NtTimeUs *out)
{
    if (!decoder || !out || decoder->mode != NT_INPUT_ESCAPE) return false;
    *out = decoder->escape_due;
    return true;
}

NtResult nt_input_expect_cursor_report(NtInputDecoder *decoder, bool expected)
{
    if (!decoder) return NT_RESULT_INVALID_ARGUMENT;
    decoder->cursor_report = expected;
    return NT_RESULT_OK;
}

NtResult nt_input_cancel(NtInputDecoder *decoder, NtTimeUs now, NtCancelReason reason, NtEvent *out)
{
    if (!decoder || !out || now < decoder->last_time) return NT_RESULT_INVALID_ARGUMENT;
    const NtEvent event = {.kind=NT_EVENT_HOST_CANCEL,.at=now,.data.reason=reason};
    if (nt_event_validate(&event) != NT_RESULT_OK) return NT_RESULT_INVALID_ARGUMENT;
    decoder->last_time = now;
    decoder->mode = NT_INPUT_GROUND;
    decoder->cursor_report = false;
    decoder->pressed = 0;
    decoder->paste_count = decoder->paste_match = 0;
    *out = event;
    return NT_RESULT_OK;
}

void nt_input_key(NtInputRead *out, NtTimeUs now, uint32_t key, uint32_t mods, NtKeyState state)
{
    out->status = NT_INPUT_READY;
    out->event = (NtEvent){.kind=NT_EVENT_KEY,.at=now,.data.key={key,mods,state}};
}

void nt_input_scalar(NtInputDecoder *decoder, NtInputRead *out, NtTimeUs now, uint32_t scalar)
{
    uint32_t mods = decoder->alt ? NT_MOD_ALT : 0;
    if (nt_input_printable(scalar)) {
        uint32_t key = scalar;
        if (scalar >= 'A' && scalar <= 'Z') {
            key = scalar - 'A' + 'a';
            mods |= NT_MOD_SHIFT;
        }
        nt_input_key(out, now, key, mods, NT_KEY_PRESS);
        if (!decoder->alt) {
            memcpy(decoder->text, decoder->sequence, decoder->count);
            out->key_text = (NtByteView){decoder->text,decoder->count};
        }
        return;
    }
    switch (scalar) {
        case 0x08: case 0x7f: nt_input_key(out,now,NT_KEY_BACKSPACE,mods,NT_KEY_PRESS); return;
        case 0x09: nt_input_key(out,now,NT_KEY_TAB,mods,NT_KEY_PRESS); return;
        case 0x0a: case 0x0d: nt_input_key(out,now,NT_KEY_ENTER,mods,NT_KEY_PRESS); return;
        default: break;
    }
    if (scalar < 0x20 && scalar != ESC) {
        const uint32_t key = scalar == 0 ? ' ' :
            scalar <= 26 ? 'a' + scalar - 1 : '\\' + scalar - 28;
        nt_input_key(out,now,key,mods | NT_MOD_CONTROL,NT_KEY_PRESS);
    } else out->status = NT_INPUT_DISCARDED;
}

static void raw_byte(NtInputDecoder *decoder, unsigned char byte, bool alt,
                     NtTimeUs now, NtInputRead *out)
{
    decoder->mode = NT_INPUT_GROUND;
    decoder->alt = alt;
    decoder->sequence[0] = (char)byte;
    decoder->count = 1;
    if (byte < 0x80) {
        nt_input_scalar(decoder,out,now,byte);
        return;
    }
    if (byte >= 0xc2 && byte <= 0xdf) decoder->utf8_size = 2;
    else if (byte >= 0xe0 && byte <= 0xef) decoder->utf8_size = 3;
    else if (byte >= 0xf0 && byte <= 0xf4) decoder->utf8_size = 4;
    else {
        out->status = NT_INPUT_DISCARDED;
        return;
    }
    decoder->mode = NT_INPUT_UTF8;
}

static void bare_escape(NtInputDecoder *decoder, NtInputRead *out)
{
    decoder->mode = NT_INPUT_GROUND;
    nt_input_key(out,decoder->escape_at,NT_KEY_ESCAPE,0,NT_KEY_PRESS);
}

static void escape_byte(NtInputDecoder *decoder, unsigned char byte, NtTimeUs now,
                        bool *take, NtInputRead *out)
{
    if (byte == ESC) {
        *take = false;
        bare_escape(decoder,out);
    } else if (byte == '[' || byte == 'O') {
        decoder->mode = byte == '[' ? NT_INPUT_CSI : NT_INPUT_SS3;
        decoder->count = 0;
        decoder->discard = false;
    } else if (byte == ']' || byte == 'P' || byte == 'X' || byte == '^' || byte == '_') {
        decoder->mode = NT_INPUT_STRING;
        decoder->osc = byte == ']';
        decoder->string_escape = false;
    } else raw_byte(decoder,byte,true,now,out);
}

static void utf8_byte(NtInputDecoder *decoder, unsigned char byte, NtTimeUs now,
                      bool *take, NtInputRead *out)
{
    if ((byte & 0xc0) != 0x80) {
        *take = false;
        decoder->mode = NT_INPUT_GROUND;
        out->status = NT_INPUT_DISCARDED;
        return;
    }
    decoder->sequence[decoder->count++] = (char)byte;
    if (decoder->count != decoder->utf8_size) return;
    decoder->mode = NT_INPUT_GROUND;
    size_t offset = 0;
    uint32_t scalar;
    const NtByteView source = {decoder->sequence,decoder->count};
    if (nt_utf8_decode(source,&offset,&scalar) != NT_RESULT_OK) out->status = NT_INPUT_DISCARDED;
    else nt_input_scalar(decoder,out,now,scalar);
}

static void sequence_byte(NtInputDecoder *decoder, unsigned char byte, NtTimeUs now,
                          bool *take, NtInputRead *out)
{
    if (byte == ESC) {
        *take = false;
        decoder->mode = NT_INPUT_GROUND;
        out->status = NT_INPUT_DISCARDED;
    } else if (byte >= 0x40 && byte <= 0x7e) {
        const NtInputMode mode = decoder->mode;
        decoder->mode = NT_INPUT_GROUND;
        if (decoder->discard) out->status = NT_INPUT_DISCARDED;
        else nt_input_sequence(decoder,out,now,mode,byte);
    } else if (byte < 0x20 || byte > 0x3f || decoder->count == NT_INPUT_SEQUENCE_LIMIT) {
        decoder->discard = true;
    } else if (!decoder->discard) decoder->sequence[decoder->count++] = (char)byte;
}

static NtResult grow_paste(NtInputDecoder *decoder, size_t required)
{
    if (required <= decoder->paste_capacity) return NT_RESULT_OK;
    const size_t limit = decoder->options.paste_limit;
    size_t capacity = decoder->paste_capacity;
    if (!capacity) capacity = limit < 64 ? limit : 64;
    while (capacity < required)
        capacity = capacity > limit / 2 ? limit : capacity * 2;
    char *copy = nt_allocate(&decoder->allocator,capacity,1);
    if (!copy) return NT_RESULT_ALLOCATION_FAILED;
    if (decoder->paste_count) memcpy(copy,decoder->paste,decoder->paste_count);
    nt_release(&decoder->allocator,decoder->paste,decoder->paste_capacity,1);
    decoder->paste = copy;
    decoder->paste_capacity = capacity;
    return NT_RESULT_OK;
}

static NtResult paste_byte(NtInputDecoder *decoder, unsigned char byte, NtTimeUs now, NtInputRead *out)
{
    static const char end[] = "\x1b[201~";
    const size_t match = decoder->paste_match;
    if (byte == (unsigned char)end[match]) {
        if (++decoder->paste_match != sizeof(end) - 1) return NT_RESULT_OK;
        const NtByteView text = {decoder->paste,decoder->paste_count};
        decoder->mode = NT_INPUT_GROUND;
        decoder->paste_match = 0;
        if (decoder->discard || nt_utf8_validate(text,NULL) != NT_RESULT_OK)
            out->status = NT_INPUT_DISCARDED;
        else {
            out->status = NT_INPUT_READY;
            out->event = (NtEvent){.kind=NT_EVENT_PASTE,.at=now,.data.text=text};
        }
        return NT_RESULT_OK;
    }
    const size_t added = match + (byte != ESC);
    if (!decoder->discard && added > decoder->options.paste_limit - decoder->paste_count)
        decoder->discard = true;
    if (!decoder->discard && added) {
        const NtResult result = grow_paste(decoder,decoder->paste_count + added);
        if (result != NT_RESULT_OK) return result;
        if (match) memcpy(decoder->paste + decoder->paste_count,end,match);
        decoder->paste_count += match;
        if (byte != ESC) decoder->paste[decoder->paste_count++] = (char)byte;
    }
    decoder->paste_match = byte == ESC ? 1 : 0;
    return NT_RESULT_OK;
}

static NtResult step(NtInputDecoder *decoder, unsigned char byte, NtTimeUs now,
                     bool *take, NtInputRead *out)
{
    switch (decoder->mode) {
        case NT_INPUT_GROUND:
            if (byte == ESC) {
                decoder->mode = NT_INPUT_ESCAPE;
                decoder->escape_at = now;
                const NtTimeUs delay = decoder->options.escape_delay;
                decoder->escape_due = now > UINT64_MAX - delay ? UINT64_MAX : now + delay;
            } else raw_byte(decoder,byte,false,now,out);
            break;
        case NT_INPUT_ESCAPE: escape_byte(decoder,byte,now,take,out); break;
        case NT_INPUT_UTF8: utf8_byte(decoder,byte,now,take,out); break;
        case NT_INPUT_CSI: case NT_INPUT_SS3: sequence_byte(decoder,byte,now,take,out); break;
        case NT_INPUT_STRING:
            if ((decoder->osc && byte == 0x07) || (decoder->string_escape && byte == '\\')) {
                decoder->mode = NT_INPUT_GROUND;
                out->status = NT_INPUT_DISCARDED;
            } else decoder->string_escape = byte == ESC;
            break;
        case NT_INPUT_PASTE: return paste_byte(decoder,byte,now,out);
    }
    return NT_RESULT_OK;
}

NtResult nt_input_read(NtInputDecoder *decoder, NtByteView bytes, NtTimeUs now, NtInputRead *out)
{
    if (!decoder || !out || (bytes.count && !bytes.data) || now < decoder->last_time)
        return NT_RESULT_INVALID_ARGUMENT;
    decoder->last_time = now;
    NtInputRead read = {0};
    NtResult result = NT_RESULT_OK;
    if (decoder->mode == NT_INPUT_ESCAPE && now >= decoder->escape_due) bare_escape(decoder,&read);
    while (read.status == NT_INPUT_PENDING && read.consumed < bytes.count) {
        bool take = true;
        result = step(decoder,(unsigned char)bytes.data[read.consumed],now,&take,&read);
        if (result != NT_RESULT_OK) break;
        if (take) ++read.consumed;
    }
    *out = read;
    return result;
}
