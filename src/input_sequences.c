#include "input_internal.h"

static size_t fields(NtByteView bytes, char separator, NtByteView *out, size_t capacity)
{
    size_t first = 0, count = 0;
    for (size_t i = 0; i <= bytes.count; ++i) {
        if (i != bytes.count && bytes.data[i] != separator) continue;
        if (count == capacity) return 0;
        out[count++] = (NtByteView){bytes.data + first,i - first};
        first = i + 1;
    }
    return count;
}

static bool decimal(NtByteView field, uint32_t *out)
{
    if (!field.count) return false;
    uint32_t value = 0;
    for (size_t i = 0; i < field.count; ++i) {
        const unsigned char byte = (unsigned char)field.data[i];
        if (byte < '0' || byte > '9') return false;
        const uint32_t digit = byte - '0';
        if (value > (UINT32_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

static bool modifiers(NtByteView field, bool kitty, uint32_t *flags, NtKeyState *state)
{
    uint32_t mask = 1, type = 1;
    NtByteView parts[2];
    if (field.count) {
        const size_t n = fields(field,':',parts,2);
        if (!n || !decimal(parts[0],&mask) || !mask) return false;
        if (n == 2 && (!kitty || !decimal(parts[1],&type) || type < 1 || type > 3)) return false;
    }
    --mask;
    if (mask > (kitty ? 255u : 15u)) return false;
    *flags = kitty ? mask : (mask & 7u) | (mask & 8u ? NT_MOD_META : 0u);
    *state = (NtKeyState)(type - 1);
    return true;
}

static size_t encode_scalar(char *out, uint32_t scalar)
{
    if (scalar <= 0x7f) {
        out[0] = (char)scalar;
        return 1;
    }
    if (scalar <= 0x7ff) {
        out[0] = (char)(0xc0u | (scalar >> 6));
        out[1] = (char)(0x80u | (scalar & 0x3f));
        return 2;
    }
    if (scalar <= 0xffff) {
        out[0] = (char)(0xe0u | (scalar >> 12));
        out[1] = (char)(0x80u | ((scalar >> 6) & 0x3f));
        out[2] = (char)(0x80u | (scalar & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0u | (scalar >> 18));
    out[1] = (char)(0x80u | ((scalar >> 12) & 0x3f));
    out[2] = (char)(0x80u | ((scalar >> 6) & 0x3f));
    out[3] = (char)(0x80u | (scalar & 0x3f));
    return 4;
}

static bool associated_text(NtInputDecoder *decoder, NtByteView field, size_t *count)
{
    size_t start = 0;
    for (size_t end = 0; end <= field.count; ++end) {
        if (end != field.count && field.data[end] != ':') continue;
        uint32_t scalar;
        if (!decimal((NtByteView){field.data + start,end - start},&scalar) ||
            !nt_input_printable(scalar)) return false;
        char utf8[4];
        const size_t size = encode_scalar(utf8,scalar);
        if (size > sizeof(decoder->text) - *count) return false;
        for (size_t i = 0; i < size; ++i) decoder->text[(*count)++] = utf8[i];
        start = end + 1;
    }
    return true;
}

static uint32_t scalar_key(uint32_t scalar, bool unicode)
{
    switch (scalar) {
        case 8: case 127: return NT_KEY_BACKSPACE;
        case 9: return NT_KEY_TAB;
        case 10: case 13: return NT_KEY_ENTER;
        case 27: return NT_KEY_ESCAPE;
        default: break;
    }
    if (unicode && scalar >= 57344 && scalar <= 63743) return 0;
    if (!nt_input_printable(scalar)) return 0;
    return scalar >= 'A' && scalar <= 'Z' ? scalar - 'A' + 'a' : scalar;
}

static void unicode_key(NtInputDecoder *decoder, NtByteView *parts, size_t n,
                         NtTimeUs now, NtInputRead *out)
{
    NtByteView keys[3];
    const size_t key_count = fields(parts[0],':',keys,3);
    uint32_t scalar, shifted = 0, base;
    if (!key_count || !decimal(keys[0],&scalar) ||
        (key_count > 1 && keys[1].count &&
         (!decimal(keys[1],&shifted) || !nt_input_printable(shifted))) ||
        (key_count > 2 && (!decimal(keys[2],&base) || !nt_input_printable(base)))) return;
    uint32_t mods;
    NtKeyState state;
    if (!modifiers(parts[1],true,&mods,&state)) return;
    if (shifted && !(mods & NT_MOD_SHIFT)) return;
    size_t size = 0;
    if (n == 3 && (!associated_text(decoder,parts[2],&size) || state == NT_KEY_RELEASE)) return;
    if (scalar == 0) {
        if (size && state == NT_KEY_PRESS) {
            out->status = NT_INPUT_READY;
            out->event = (NtEvent){.kind=NT_EVENT_TEXT,.at=now,.data.text={decoder->text,size}};
        }
        return;
    }
    const uint32_t key = scalar_key(scalar,true);
    if (!key) return;
    nt_input_key(out,now,key,mods,state);
    out->key_text = (NtByteView){size ? decoder->text : NULL,size};
}

static uint32_t tilde_key(uint32_t number)
{
    static const uint32_t keys[] = {
        0,NT_KEY_HOME,NT_KEY_INSERT,NT_KEY_DELETE,NT_KEY_END,NT_KEY_PAGE_UP,NT_KEY_PAGE_DOWN,
        NT_KEY_HOME,NT_KEY_END,0,0,NT_KEY_F1,NT_KEY_F2,NT_KEY_F3,NT_KEY_F4,NT_KEY_F5,
        0,NT_KEY_F6,NT_KEY_F7,NT_KEY_F8,NT_KEY_F9,NT_KEY_F10,0,NT_KEY_F11,NT_KEY_F12
    };
    return number < sizeof(keys) / sizeof(keys[0]) ? keys[number] : 0;
}

static uint32_t letter_key(unsigned char final, NtInputMode mode, bool kitty)
{
    switch (final) {
        case 'A': return NT_KEY_UP;
        case 'B': return NT_KEY_DOWN;
        case 'C': return NT_KEY_RIGHT;
        case 'D': return NT_KEY_LEFT;
        case 'H': return NT_KEY_HOME;
        case 'F': return NT_KEY_END;
        case 'P': return NT_KEY_F1;
        case 'Q': return NT_KEY_F2;
        case 'R': return mode == NT_INPUT_SS3 || !kitty ? NT_KEY_F3 : 0;
        case 'S': return NT_KEY_F4;
        case 'Z': return NT_KEY_TAB;
        default: return 0;
    }
}

static void emit_wheel(NtInputRead *out, NtTimeUs now, NtCellPoint position,
                       uint32_t button, uint32_t mods)
{
    NtWheelInput wheel = {.position=position,.modifiers=mods};
    if (button < 2) wheel.dy = button ? 1 : -1;
    else wheel.dx = button == 3 ? 1 : -1;
    out->event = (NtEvent){.kind=NT_EVENT_WHEEL,.at=now,.data.wheel=wheel};
    out->status = NT_INPUT_READY;
}

static void mouse(NtInputDecoder *decoder, NtByteView bytes, unsigned char final,
                  NtTimeUs now, NtInputRead *out)
{
    NtByteView values[3];
    uint32_t code, x, y;
    const uint32_t limit = (uint32_t)INT32_MAX + 1u;
    if ((final != 'M' && final != 'm') || fields(bytes,';',values,3) != 3 ||
        !decimal(values[0],&code) || code > 255 ||
        !decimal(values[1],&x) || x == 0 || x > limit ||
        !decimal(values[2],&y) || y == 0 || y > limit) return;

    const uint32_t low = code & 3u;
    const uint32_t mods = ((code & 4u) ? NT_MOD_SHIFT : 0u) |
        ((code & 8u) ? NT_MOD_ALT : 0u) | ((code & 16u) ? NT_MOD_CONTROL : 0u);
    const NtCellPoint position = {(int32_t)(x - 1),(int32_t)(y - 1)};
    const bool motion = (code & 32u) != 0;
    if (code & 64u) {
        if (final == 'M' && !motion && !(code & 128u)) emit_wheel(out,now,position,low,mods);
        return;
    }
    const bool extended = (code & 128u) != 0;
    const uint32_t button = extended ? 1u << (low + 3) : low < 3 ? 1u << low : 0;
    NtEventKind kind = NT_EVENT_POINTER_MOVE;
    if (motion) {
        if (final != 'M' || (button && !(decoder->pressed & button)) ||
            (!button && decoder->pressed)) return;
    } else {
        if (!button) return;
        const bool down = (decoder->pressed & button) != 0;
        if (final == 'M') {
            if (down) return;
            kind = NT_EVENT_POINTER_DOWN;
            decoder->pressed |= button;
        } else {
            if (!down) return;
            kind = NT_EVENT_POINTER_UP;
            decoder->pressed &= ~button;
        }
    }
    out->event = (NtEvent){.kind=kind,.at=now,
        .data.pointer={0,position,motion ? 0 : button,decoder->pressed,true,mods}};
    out->status = NT_INPUT_READY;
}

static void xterm_character(NtInputDecoder *decoder, uint32_t scalar, uint32_t mods,
                            NtTimeUs now, NtInputRead *out)
{
    const uint32_t key = scalar_key(scalar,false);
    if (!key) return;
    nt_input_key(out,now,key,mods,NT_KEY_PRESS);
    if (nt_input_printable(scalar) && !(mods & ~(uint32_t)NT_MOD_SHIFT))
        out->key_text = (NtByteView){decoder->text,encode_scalar(decoder->text,scalar)};
}

static void legacy_key(NtInputDecoder *decoder, NtByteView *parts, size_t n, NtInputMode mode,
                        unsigned char final, NtTimeUs now, NtInputRead *out)
{
    uint32_t parameter = 1, mods;
    NtKeyState state;
    const bool kitty = decoder->options.keyboard == NT_KEYBOARD_KITTY;
    if ((parts[0].count && !decimal(parts[0],&parameter)) ||
        !modifiers(parts[1],kitty,&mods,&state)) return;
    if (n == 3) {
        uint32_t scalar;
        if (kitty || mode != NT_INPUT_CSI || final != '~' || parameter != 27 ||
            !decimal(parts[2],&scalar)) return;
        xterm_character(decoder,scalar,mods,now,out);
        return;
    }
    uint32_t key;
    if (final == '~') {
        if (mode != NT_INPUT_CSI || !parts[0].count) return;
        if (parameter == 200 && n == 1) {
            decoder->mode = NT_INPUT_PASTE;
            decoder->discard = false;
            decoder->paste_count = decoder->paste_match = 0;
            out->status = NT_INPUT_PENDING;
            return;
        }
        key = tilde_key(parameter);
    } else {
        if (parameter != 1) return;
        key = letter_key(final,mode,kitty);
        if (final == 'Z') mods |= NT_MOD_SHIFT;
    }
    if (key) nt_input_key(out,now,key,mods,state);
}

void nt_input_sequence(NtInputDecoder *decoder, NtInputRead *out, NtTimeUs now,
                       NtInputMode mode, unsigned char final)
{
    out->status = NT_INPUT_DISCARDED;
    const NtByteView bytes = {decoder->sequence,decoder->count};
    if (mode == NT_INPUT_CSI && bytes.count && bytes.data[0] == '<') {
        mouse(decoder,(NtByteView){bytes.data + 1,bytes.count - 1},final,now,out);
        return;
    }
    if (mode == NT_INPUT_CSI && final == 'O' && !bytes.count) {
        decoder->pressed = 0;
        out->event = (NtEvent){.kind=NT_EVENT_HOST_CANCEL,.at=now,.data.reason=NT_CANCEL_FOCUS_LOSS};
        out->status = NT_INPUT_READY;
        return;
    }
    NtByteView parts[3] = {{0}};
    const size_t n = fields(bytes,';',parts,3);
    if (!n) return;
    if (decoder->cursor_report && mode == NT_INPUT_CSI && final == 'R' && n == 2) {
        uint32_t row, column;
        if (decimal(parts[0],&row) && decimal(parts[1],&column) && row && column) {
            decoder->cursor_report = false;
            return;
        }
    }
    if (mode == NT_INPUT_CSI && final == 'u') {
        if (decoder->options.keyboard == NT_KEYBOARD_KITTY) {
            unicode_key(decoder,parts,n,now,out);
        } else {
            uint32_t scalar, mods;
            NtKeyState state;
            if (n <= 2 && decimal(parts[0],&scalar) && modifiers(parts[1],false,&mods,&state))
                xterm_character(decoder,scalar,mods,now,out);
        }
    } else if (mode == NT_INPUT_CSI && final == 't') {
        uint32_t type, rows, columns;
        if (n != 3 || !decimal(parts[0],&type) || type != 8 ||
            !decimal(parts[1],&rows) || !decimal(parts[2],&columns)) return;
        const NtEvent event = {.kind=NT_EVENT_RESIZE,.at=now,.data.resize={columns,rows}};
        if (nt_event_validate(&event) == NT_RESULT_OK) {
            out->event = event;
            out->status = NT_INPUT_READY;
        }
    } else legacy_key(decoder,parts,n,mode,final,now,out);
}
