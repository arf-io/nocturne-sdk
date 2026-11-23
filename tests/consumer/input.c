#include <nocturne/input.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Input consumer line %d: %s\n",__LINE__,#condition); \
    exit(EXIT_FAILURE); \
} } while (0)
#define BYTES(value) ((NtByteView){value,sizeof(value)-1})

static NtInputRead read_input(NtInputDecoder *decoder, NtTimeUs time, NtByteView bytes,
                              NtInputStatus expected)
{
    NtInputRead out;
    CHECK(nt_input_read(decoder,bytes,time,&out) == NT_RESULT_OK);
    CHECK(out.status == expected);
    CHECK(out.consumed <= bytes.count);
    if (expected == NT_INPUT_READY) CHECK(nt_event_validate(&out.event) == NT_RESULT_OK);
    return out;
}

static NtInputRead complete(NtInputDecoder *decoder, NtTimeUs time, NtByteView bytes,
                            NtInputStatus expected)
{
    const NtInputRead out = read_input(decoder,time,bytes,expected);
    CHECK(out.consumed == bytes.count);
    return out;
}

static void equal_text(NtByteView actual, NtByteView expected)
{
    CHECK(actual.count == expected.count);
    CHECK(!expected.count || memcmp(actual.data,expected.data,expected.count) == 0);
}

static void key(NtInputRead out, uint32_t id, uint32_t mods, NtKeyState state, NtByteView text)
{
    CHECK(out.status == NT_INPUT_READY && out.event.kind == NT_EVENT_KEY);
    CHECK(out.event.data.key.key == id && out.event.data.key.modifiers == mods);
    CHECK(out.event.data.key.state == state);
    equal_text(out.key_text,text);
}

static void fragments(NtInputDecoder *decoder, NtByteView input, size_t split, NtInputRead *out)
{
    if (split)
        complete(decoder,3,(NtByteView){input.data,split},NT_INPUT_PENDING);
    *out = complete(decoder,4,(NtByteView){input.data + split,input.count - split},NT_INPUT_READY);
    CHECK(out->event.at == 4);
}

static void fragmented_keyboard_and_paste(void)
{
    const struct {
        NtByteView bytes;
        uint32_t key, modifiers;
        NtKeyState state;
        NtByteView text;
        NtKeyboardProtocol protocol;
    } samples[] = {
        {BYTES("é"),0xe9,0,NT_KEY_PRESS,BYTES("é")},
        {BYTES("界"),0x754c,0,NT_KEY_PRESS,BYTES("界")},
        {BYTES("🙂"),0x1f642,0,NT_KEY_PRESS,BYTES("🙂")},
        {BYTES("\x1b" "界"),0x754c,NT_MOD_ALT,NT_KEY_PRESS,{0}},
        {BYTES("\x1b[1;6A"),NT_KEY_UP,NT_MOD_CONTROL|NT_MOD_SHIFT,NT_KEY_PRESS,{0}},
        {BYTES("\x1b" "OR"),NT_KEY_F3,0,NT_KEY_PRESS,{0}},
        {BYTES("\x1b[24~"),NT_KEY_F12,0,NT_KEY_PRESS,{0}},
        {BYTES("\x1b[Z"),NT_KEY_TAB,NT_MOD_SHIFT,NT_KEY_PRESS,{0}},
        {BYTES("\x1b[1;9D"),NT_KEY_LEFT,NT_MOD_META,NT_KEY_PRESS,{0}},
        {BYTES("\x1b[27;3;120~"),'x',NT_MOD_ALT,NT_KEY_PRESS,{0}},
        {BYTES("\x1b[1;2R"),NT_KEY_F3,NT_MOD_SHIFT,NT_KEY_PRESS,{0}},
        {BYTES("\x1b[97;9u"),'a',NT_MOD_META,NT_KEY_PRESS,{0}},
        {BYTES("\x1b[97;6:2u"),'a',NT_MOD_SHIFT|NT_MOD_CONTROL,NT_KEY_REPEAT,{0},NT_KEYBOARD_KITTY},
        {BYTES("\x1b[1;9:3A"),NT_KEY_UP,NT_MOD_SUPER,NT_KEY_RELEASE,{0},NT_KEYBOARD_KITTY},
        {BYTES("\x1b[1;9A"),NT_KEY_UP,NT_MOD_SUPER,NT_KEY_PRESS,{0},NT_KEYBOARD_KITTY},
        {BYTES("\x1b[97;1:2u"),'a',0,NT_KEY_REPEAT,{0},NT_KEYBOARD_KITTY},
        {BYTES("\x1b[97:65:97;2;65:769u"),'a',NT_MOD_SHIFT,NT_KEY_PRESS,BYTES("A\xcc\x81"),NT_KEYBOARD_KITTY},
        {BYTES("\x1b[97;66u"),'a',NT_MOD_CAPS_LOCK|NT_MOD_SHIFT,NT_KEY_PRESS,{0},NT_KEYBOARD_KITTY}
    };
    for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); ++i) {
        for (size_t split = 0; split < samples[i].bytes.count; ++split) {
            NtInputDecoder *decoder;
            const NtInputOptions options = {25000,1024,samples[i].protocol};
            CHECK(nt_input_create(NULL,&options,&decoder) == NT_RESULT_OK);
            NtInputRead out;
            fragments(decoder,samples[i].bytes,split,&out);
            key(out,samples[i].key,samples[i].modifiers,samples[i].state,samples[i].text);
            nt_input_destroy(decoder);
        }
    }

    const NtByteView paste = BYTES("\x1b[200~é\0\r\n\x1b[20\x1b[201~");
    const NtByteView content = BYTES("é\0\r\n\x1b[20");
    for (size_t split = 0; split < paste.count; ++split) {
        NtInputDecoder *decoder;
        CHECK(nt_input_create(NULL,NULL,&decoder) == NT_RESULT_OK);
        NtInputRead out;
        fragments(decoder,paste,split,&out);
        CHECK(out.event.kind == NT_EVENT_PASTE && !out.key_text.count);
        equal_text(out.event.data.text,content);
        nt_input_destroy(decoder);
    }
    NtInputDecoder *decoder;
    const NtInputOptions kitty = {25000,1024,NT_KEYBOARD_KITTY};
    CHECK(nt_input_create(NULL,&kitty,&decoder) == NT_RESULT_OK);
    for (size_t i = 0; i + 1 < paste.count; ++i)
        complete(decoder,20,(NtByteView){paste.data+i,1},NT_INPUT_PENDING);
    const NtInputRead out = complete(decoder,20,(NtByteView){paste.data+paste.count-1,1},NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_PASTE);
    equal_text(out.event.data.text,content);
    const NtInputRead text = complete(decoder,21,BYTES("\x1b[0;;229:769u"),NT_INPUT_READY);
    CHECK(text.event.kind == NT_EVENT_TEXT);
    equal_text(text.event.data.text,BYTES("å\xcc\x81"));
    complete(decoder,22,BYTES("\x1b[1;2R"),NT_INPUT_DISCARDED);
    nt_input_destroy(decoder);
}

static void time_and_input_recovery(void)
{
    const NtInputOptions options = {10,4,NT_KEYBOARD_XTERM};
    NtInputDecoder *decoder;
    CHECK(nt_input_create(NULL,&options,&decoder) == NT_RESULT_OK);
    complete(decoder,100,BYTES("\x1b"),NT_INPUT_PENDING);
    NtTimeUs deadline;
    CHECK(nt_input_deadline(decoder,&deadline) && deadline == 110);
    complete(decoder,109,(NtByteView){0},NT_INPUT_PENDING);
    NtInputRead out = read_input(decoder,110,BYTES("A"),NT_INPUT_READY);
    key(out,NT_KEY_ESCAPE,0,NT_KEY_PRESS,(NtByteView){0});
    CHECK(!out.consumed && out.event.at == 100 && !nt_input_deadline(decoder,&deadline));
    key(complete(decoder,110,BYTES("A"),NT_INPUT_READY),'a',NT_MOD_SHIFT,NT_KEY_PRESS,BYTES("A"));
    CHECK(nt_input_read(decoder,BYTES("a"),109,&out) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(out.event.data.key.key == NT_KEY_ESCAPE);
    key(complete(decoder,111,BYTES("\x03"),NT_INPUT_READY),'c',NT_MOD_CONTROL,NT_KEY_PRESS,(NtByteView){0});
    key(complete(decoder,111,BYTES("\0"),NT_INPUT_READY),' ',NT_MOD_CONTROL,NT_KEY_PRESS,(NtByteView){0});
    complete(decoder,111,BYTES("\xc2"),NT_INPUT_PENDING);
    out = read_input(decoder,111,BYTES("z"),NT_INPUT_DISCARDED);
    CHECK(!out.consumed);
    key(complete(decoder,111,BYTES("z"),NT_INPUT_READY),'z',0,NT_KEY_PRESS,BYTES("z"));

    complete(decoder,111,BYTES("\x1b[42949672960;2A"),NT_INPUT_DISCARDED);
    complete(decoder,111,BYTES("\x1b[2:8u"),NT_INPUT_DISCARDED);
    complete(decoder,111,BYTES("\x1b]some terminal response\x1b"),NT_INPUT_PENDING);
    complete(decoder,111,BYTES("\\"),NT_INPUT_DISCARDED);
    complete(decoder,111,BYTES("\x1b[200~abcde\x1b[201~"),NT_INPUT_DISCARDED);
    complete(decoder,111,BYTES("\x1b[200~\xff\x1b[201~"),NT_INPUT_DISCARDED);
    out = complete(decoder,111,BYTES("\x1b[200~1234\x1b[201~"),NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_PASTE);
    equal_text(out.event.data.text,BYTES("1234"));

    char long_parameter[66];
    memset(long_parameter,'1',sizeof(long_parameter));
    complete(decoder,111,BYTES("\x1b["),NT_INPUT_PENDING);
    complete(decoder,111,(NtByteView){long_parameter,sizeof(long_parameter)},NT_INPUT_PENDING);
    complete(decoder,111,BYTES("D"),NT_INPUT_DISCARDED);
    key(complete(decoder,111,BYTES("p"),NT_INPUT_READY),'p',0,NT_KEY_PRESS,BYTES("p"));
    CHECK(nt_input_expect_cursor_report(decoder,true) == NT_RESULT_OK);
    out = complete(decoder,111,BYTES("\x1b[O"),NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_HOST_CANCEL && out.event.data.reason == NT_CANCEL_FOCUS_LOSS);
    complete(decoder,111,BYTES("\x1b[1;2R"),NT_INPUT_DISCARDED);
    key(complete(decoder,111,BYTES("\x1b[1;2R"),NT_INPUT_READY),
        NT_KEY_F3,NT_MOD_SHIFT,NT_KEY_PRESS,(NtByteView){0});
    complete(decoder,111,BYTES("\x1b[1;9:2A"),NT_INPUT_DISCARDED);
    nt_input_destroy(decoder);
}

static void mouse_resize_and_cancel(void)
{
    const NtByteView click = BYTES("\x1b[<12;2147483648;1M");
    for (size_t split = 0; split < click.count; ++split) {
        NtInputDecoder *decoder;
        CHECK(nt_input_create(NULL,NULL,&decoder) == NT_RESULT_OK);
        NtInputRead out;
        fragments(decoder,click,split,&out);
        const NtPointerInput p = out.event.data.pointer;
        CHECK(out.event.kind == NT_EVENT_POINTER_DOWN && p.position.x == INT32_MAX && !p.position.y);
        CHECK(p.button == NT_BUTTON_PRIMARY && p.pressed == NT_BUTTON_PRIMARY &&
              p.modifiers == (NT_MOD_SHIFT|NT_MOD_ALT) && p.has_position);
        nt_input_destroy(decoder);
    }

    NtInputDecoder *decoder;
    CHECK(nt_input_create(NULL,NULL,&decoder) == NT_RESULT_OK);
    NtInputRead out = complete(decoder,12,BYTES("\x1b[<0;2;3M"),NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_POINTER_DOWN);
    out = complete(decoder,12,BYTES("\x1b[<130;3;3M"),NT_INPUT_READY);
    CHECK(out.event.data.pointer.pressed == (NT_BUTTON_PRIMARY|NT_BUTTON_10));
    out = complete(decoder,12,BYTES("\x1b[<48;3;4M"),NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_POINTER_MOVE && !out.event.data.pointer.button);
    CHECK(out.event.data.pointer.pressed == (NT_BUTTON_PRIMARY|NT_BUTTON_10));
    CHECK(out.event.data.pointer.modifiers == NT_MOD_CONTROL);
    out = complete(decoder,12,BYTES("\x1b[<70;3;4M"),NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_WHEEL && out.event.data.wheel.dx == -1 &&
          !out.event.data.wheel.dy && out.event.data.wheel.modifiers == NT_MOD_SHIFT);
    out = complete(decoder,12,BYTES("\x1b[<65;3;4M"),NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_WHEEL && out.event.data.wheel.dy == 1);
    complete(decoder,12,BYTES("\x1b[<0;0;4m"),NT_INPUT_DISCARDED);
    out = complete(decoder,12,BYTES("\x1b[<0;3;4m"),NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_POINTER_UP && out.event.data.pointer.pressed == NT_BUTTON_10);
    out = complete(decoder,12,BYTES("\x1b[8;22;81t"),NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_RESIZE && out.event.data.resize.rows == 22 &&
          out.event.data.resize.columns == 81);
    out = complete(decoder,12,BYTES("\x1b[O"),NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_HOST_CANCEL && out.event.data.reason == NT_CANCEL_FOCUS_LOSS);
    complete(decoder,12,BYTES("\x1b[<130;3;3m"),NT_INPUT_DISCARDED);
    out = complete(decoder,12,BYTES("\x1b[<35;3;4M"),NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_POINTER_MOVE && !out.event.data.pointer.pressed);

    complete(decoder,12,BYTES("\x1b[200~partial"),NT_INPUT_PENDING);
    NtEvent cancel;
    CHECK(nt_input_cancel(decoder,13,NT_CANCEL_HOST,&cancel) == NT_RESULT_OK);
    CHECK(cancel.kind == NT_EVENT_HOST_CANCEL && cancel.at == 13);
    key(complete(decoder,13,BYTES("a"),NT_INPUT_READY),'a',0,NT_KEY_PRESS,BYTES("a"));
    CHECK(nt_event_validate(&(NtEvent){.kind=NT_EVENT_POINTER_UP,
          .data.pointer={.button=NT_BUTTON_PRIMARY,.pressed=NT_BUTTON_PRIMARY}}) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_event_validate(&(NtEvent){.kind=NT_EVENT_RESIZE,
          .data.resize={UINT32_MAX,1}}) == NT_RESULT_OUT_OF_RANGE);
    nt_input_destroy(decoder);
}

typedef struct Memory { size_t live; bool fail; } Memory;

static void *allocate(void *context, size_t size, size_t alignment)
{
    (void)alignment;
    Memory *memory = context;
    if (memory->fail) return NULL;
    void *block = malloc(size);
    if (block) ++memory->live;
    return block;
}

static void release(void *context, void *block, size_t size, size_t alignment)
{
    (void)size;
    (void)alignment;
    Memory *memory = context;
    CHECK(memory->live);
    --memory->live;
    free(block);
}

static void paste_allocation_retry(void)
{
    Memory memory = {0,true};
    const NtAllocator allocator = {&memory,allocate,release};
    const NtInputOptions options = {10,80,NT_KEYBOARD_XTERM};
    NtInputDecoder *decoder = NULL;
    CHECK(nt_input_create(&allocator,&options,&decoder) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(!decoder && !memory.live);
    memory.fail = false;
    CHECK(nt_input_create(&allocator,&options,&decoder) == NT_RESULT_OK);
    char payload[64];
    memset(payload,'a',sizeof(payload));
    complete(decoder,1,BYTES("\x1b[200~"),NT_INPUT_PENDING);
    complete(decoder,1,(NtByteView){payload,sizeof(payload)},NT_INPUT_PENDING);
    complete(decoder,1,BYTES("\x1b[20"),NT_INPUT_PENDING);

    memory.fail = true;
    NtInputRead out;
    const NtByteView tail = BYTES("Q\x1b[201~");
    CHECK(nt_input_read(decoder,tail,1,&out) == NT_RESULT_ALLOCATION_FAILED);
    CHECK(out.consumed == 0 && out.status == NT_INPUT_PENDING && memory.live == 2);
    memory.fail = false;
    out = complete(decoder,1,tail,NT_INPUT_READY);
    CHECK(out.event.kind == NT_EVENT_PASTE && out.event.data.text.count == sizeof(payload) + 5);
    CHECK(memcmp(out.event.data.text.data,payload,sizeof(payload)) == 0);
    CHECK(memcmp(out.event.data.text.data+sizeof(payload),"\x1b[20Q",5) == 0);
    nt_input_destroy(decoder);
    CHECK(!memory.live);
}

int main(void)
{
    fragmented_keyboard_and_paste();
    time_and_input_recovery();
    mouse_resize_and_cancel();
    paste_allocation_retry();
    puts("Fragmented input, UTF-8, keyboard, mouse, paste and allocation retry passed.");
    return EXIT_SUCCESS;
}
