#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <nocturne/paint.h>
#include <nocturne/windows_host.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Windows console check failed at line %d: %s (Win32 %lu)\n", \
        __LINE__, #condition, (unsigned long)GetLastError()); return 1; \
} } while (0)
#define OK(call) CHECK((call) == NT_RESULT_OK)

typedef struct Fixture {
    NtTree *tree;
    NtNodeId node;
    unsigned presses, repeats, releases, text, reserved, down, move, wheel, cancel, resize;
    unsigned reserved_releases;
    bool fail_reserved;
} Fixture;

static NtResult measure(const void *state, NtMeasureContext *context, NtConstraints limits, NtCellSize *out)
{
    (void)state;
    (void)context;
    (void)limits;
    *out = (NtCellSize){3, 1};
    return NT_RESULT_OK;
}

static NtResult layout(const void *state, NtLayoutContext *context, NtCellSize size)
{
    (void)state;
    (void)context;
    (void)size;
    return NT_RESULT_OK;
}

static NtResult paint(const void *state, NtPaintContext *context, NtCellSize size)
{
    (void)state;
    (void)size;
    const NtCellStyle ink = {{255, 255, 255, 255}, {0, 0, 0, 255}};
    NtResult result = nt_paint_glyph(context, (NtCellPoint){0, 0}, (NtByteView){"W", 1}, 1, ink);
    if (result != NT_RESULT_OK) return result;
    return nt_paint_glyph(context, (NtCellPoint){1, 0}, (NtByteView){"\xc3\xa9", 2}, 1, ink);
}

static NtResult event(void *state, NtEventContext *context, const NtEvent *input, NtEventReply *reply)
{
    Fixture *fixture = state;
    if (nt_event_phase(context) != NT_EVENT_TARGET) return NT_RESULT_OK;
    switch (input->kind) {
        case NT_EVENT_KEY:
            if (input->data.key.state == NT_KEY_PRESS) ++fixture->presses;
            if (input->data.key.state == NT_KEY_REPEAT) ++fixture->repeats;
            if (input->data.key.state == NT_KEY_RELEASE) ++fixture->releases;
            break;
        case NT_EVENT_TEXT:
            if ((input->data.text.count == 1 && input->data.text.data[0] == 'a') ||
                (input->data.text.count == 2 && !memcmp(input->data.text.data, "\xc3\xa9", 2)) ||
                (input->data.text.count == 4 && !memcmp(input->data.text.data, "\xf0\x9f\x92\xbb", 4)))
                ++fixture->text;
            break;
        case NT_EVENT_POINTER_DOWN: {
            NtCellPoint local;
            if (!nt_event_local_position(context, &local) || local.x != 1 || local.y != 0)
                return NT_RESULT_OUT_OF_RANGE;
            ++fixture->down;
            reply->capture = NT_CAPTURE_TAKE;
            break;
        }
        case NT_EVENT_POINTER_MOVE: ++fixture->move; break;
        case NT_EVENT_WHEEL:
            if (input->data.wheel.dy != -1 || input->data.wheel.dx)
                return NT_RESULT_OUT_OF_RANGE;
            ++fixture->wheel;
            break;
        case NT_EVENT_POINTER_CANCEL:
            if (input->data.pointer_cancel.reason == NT_CANCEL_FOCUS_LOSS) ++fixture->cancel;
            break;
        default: break;
    }
    return NT_RESULT_OK;
}

static NtResult resized(void *context, NtCellSize size)
{
    Fixture *fixture = context;
    const NtCellSize published = nt_frame_size(nt_tree_frame(fixture->tree));
    if (size.columns != published.columns || size.rows != published.rows)
        return NT_RESULT_OUT_OF_RANGE;
    ++fixture->resize;
    return nt_tree_set_focus(fixture->tree, fixture->node);
}

static NtResult reserved(void *context, const NtEvent *event, NtByteView text)
{
    Fixture *fixture = context;
    if (event->data.key.key != 'q' || text.count) return NT_RESULT_INVALID_ARGUMENT;
    ++fixture->reserved;
    if (event->data.key.state == NT_KEY_RELEASE) ++fixture->reserved_releases;
    return fixture->fail_reserved ? NT_RESULT_CALLBACK_FAILED : NT_RESULT_OK;
}

static INPUT_RECORD key(BOOL down, WORD code, WCHAR text, WORD repeat, DWORD mods)
{
    const INPUT_RECORD input = {.EventType = KEY_EVENT,
        .Event.KeyEvent = {.bKeyDown = down, .wRepeatCount = repeat, .wVirtualKeyCode = code,
                           .uChar.UnicodeChar = text, .dwControlKeyState = mods}};
    return input;
}

static bool enqueue(HANDLE handle, const INPUT_RECORD *records, DWORD count)
{
    DWORD written;
    return WriteConsoleInputW(handle, records, count, &written) && written == count;
}

static int exercise(HANDLE input, HANDLE output)
{
    DWORD old_input, old_output, mode;
    CHECK(GetConsoleMode(input, &old_input) && GetConsoleMode(output, &old_output));
    CHECK(SetConsoleMode(input, old_input | ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE));
    CHECK(GetConsoleMode(input, &old_input));
    CONSOLE_CURSOR_INFO old_cursor, cursor;
    CHECK(GetConsoleCursorInfo(output, &old_cursor));
    NtWindowsTerminalReport probe;
    OK(nt_windows_terminal_probe(input, output, &probe));
    CHECK(probe.host_available && probe.input_console && probe.output_console && probe.capabilities.utf8);
    CHECK(probe.capabilities.pointer && !probe.capabilities.bracketed_paste);
    HANDLE pipe_read, pipe_write;
    CHECK(CreatePipe(&pipe_read, &pipe_write, NULL, 0));
    OK(nt_windows_terminal_probe(input, pipe_write, &probe));
    CHECK(probe.input_console && !probe.output_console && !probe.host_available);
    CHECK(CloseHandle(pipe_read) && CloseHandle(pipe_write));
    OK(nt_windows_terminal_probe(input, output, &probe));

    Fixture fixture = {0};
    OK(nt_tree_create(NULL, &fixture.tree));
    const NtComponentOps ops = {.measure = measure, .layout = layout, .paint = paint, .event = event};
    OK(nt_tree_mount(fixture.tree, (NtNodeId){0}, &ops, &fixture,
                     (NtMountOptions){true, true, true}, &fixture.node));
    const NtWindowsHostOptions options = {
        .input_handle = input, .output_handle = output, .capabilities = probe.capabilities,
        .motion = NT_MOTION_FULL, .hide_cursor = true,
        .after_resize = resized, .resize_context = &fixture, .reserved_key = reserved, .reserved_context = &fixture,
    };
    NtWindowsHost *host;
    OK(nt_windows_host_open(fixture.tree, 100, NULL, &options, &host));
    CHECK(GetConsoleCursorInfo(output, &cursor) && !cursor.bVisible);
    NtWindowsHost *other = host;
    CHECK(nt_windows_host_open(fixture.tree, 100, NULL, &options, &other) == NT_RESULT_WRONG_PHASE && other == host);
    CHECK(GetConsoleMode(input, &mode) && (mode & ENABLE_MOUSE_INPUT) && !(mode & ENABLE_PROCESSED_INPUT));
    CHECK((mode & ENABLE_INSERT_MODE) && !(mode & ENABLE_QUICK_EDIT_MODE));
    CHECK(GetConsoleMode(output, &mode) && (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING));
    CHECK(FlushConsoleInputBuffer(input));
    NtWindowsStep report = {.input_events = 999};
    CHECK(nt_windows_host_step(host, 99, NULL, &report) == NT_RESULT_OUT_OF_RANGE && report.input_events == 999);
    const NtKeyChord malformed = {UINT32_MAX, 0};
    const NtDispatchPolicy malformed_policy = {.reserved = &malformed, .reserved_count = 1};
    CHECK(nt_windows_host_step(host, 101, &malformed_policy, &report) == NT_RESULT_INVALID_ARGUMENT &&
          report.input_events == 999);
    OK(nt_windows_host_step(host, 100, NULL, &report));
    CHECK(report.resized && report.presented && fixture.resize == 1 && report.input_events == 0);
    CONSOLE_SCREEN_BUFFER_INFO screen;
    CHECK(GetConsoleScreenBufferInfo(output, &screen));
    CHECK(report.size.columns == (uint32_t)(screen.srWindow.Right - screen.srWindow.Left + 1));
    CHECK(report.size.rows == (uint32_t)(screen.srWindow.Bottom - screen.srWindow.Top + 1));
    WCHAR glyphs[2];
    DWORD read;
    CHECK(ReadConsoleOutputCharacterW(output, glyphs, 2,
        (COORD){screen.srWindow.Left, screen.srWindow.Top}, &read) && read == 2 &&
        glyphs[0] == 'W' && glyphs[1] == 0xe9);
    OK(nt_tree_invalidate(fixture.tree, fixture.node, NT_INVALIDATE_PAINT));
    OK(nt_windows_host_step(host, 100, NULL, &report));
    CHECK(report.presented && !report.resized);
    CONSOLE_SCREEN_BUFFER_INFO refreshed;
    CHECK(GetConsoleScreenBufferInfo(output, &refreshed) &&
        refreshed.srWindow.Left == screen.srWindow.Left && refreshed.srWindow.Top == screen.srWindow.Top);

    const INPUT_RECORD keys[] = {
        key(TRUE, 'A', 'a', 140, 0), key(FALSE, 'A', 'a', 1, 0),
        key(TRUE, 'Q', 17, 1, LEFT_CTRL_PRESSED),
        key(TRUE, VK_PACKET, 0xd83d, 1, 0), key(TRUE, VK_PACKET, 0xdcbb, 1, 0),
    };
    const NtKeyChord chords[] = {
        {'q', NT_MOD_CONTROL},
        {'q', NT_MOD_CONTROL | NT_MOD_NUM_LOCK},
        {'q', NT_MOD_CONTROL | NT_MOD_CAPS_LOCK},
        {'q', NT_MOD_CONTROL | NT_MOD_NUM_LOCK | NT_MOD_CAPS_LOCK},
    };
    const NtDispatchPolicy policy = {.reserved = chords, .reserved_count = sizeof(chords) / sizeof(*chords)};
    CHECK(enqueue(input, keys, sizeof(keys) / sizeof(*keys)));
    OK(nt_windows_host_step(host, 101, &policy, &report));
    CHECK(report.input_events > 0 && report.input_events < 140 && fixture.releases == 0);
    OK(nt_windows_host_step(host, 102, &policy, &report));
    CHECK(fixture.presses == 2 && fixture.repeats == 139 && fixture.releases == 1);
    CHECK(fixture.reserved == 1 && fixture.text == 141);

    const INPUT_RECORD high[] = {key(TRUE, 0, 0xd83d, 1, 0), key(FALSE, 0, 0xd83d, 1, 0)};
    const INPUT_RECORD low_and_alt[] = {
        key(TRUE, 0, 0xdcbb, 1, 0), key(FALSE, 0, 0xdcbb, 1, 0),
        key(TRUE, VK_MENU, 0, 1, LEFT_ALT_PRESSED), key(FALSE, VK_MENU, 0xe9, 1, 0),
        key(FALSE, 'Q', 17, 1, LEFT_CTRL_PRESSED | NUMLOCK_ON | CAPSLOCK_ON),
        key(TRUE, 'Q', 17, 1, LEFT_CTRL_PRESSED | NUMLOCK_ON | CAPSLOCK_ON),
    };
    CHECK(enqueue(input, high, sizeof(high) / sizeof(*high)));
    OK(nt_windows_host_step(host, 103, &policy, &report));
    CHECK(report.input_events == 0 && fixture.text == 141);
    CHECK(enqueue(input, low_and_alt, sizeof(low_and_alt) / sizeof(*low_and_alt)));
    OK(nt_windows_host_step(host, 104, &policy, &report));
    CHECK(report.input_events == 5 && fixture.presses == 3 && fixture.releases == 2 && fixture.text == 143);
    CHECK(fixture.reserved == 3 && fixture.reserved_releases == 1);

    const COORD position = {(SHORT)(screen.srWindow.Left + 1), screen.srWindow.Top};
    const INPUT_RECORD mouse[] = {
        {.EventType = MOUSE_EVENT, .Event.MouseEvent = {position, FROM_LEFT_1ST_BUTTON_PRESSED, 0, 0}},
        {.EventType = MOUSE_EVENT, .Event.MouseEvent = {position, FROM_LEFT_1ST_BUTTON_PRESSED, 0, MOUSE_MOVED}},
        {.EventType = MOUSE_EVENT, .Event.MouseEvent = {position, 60u << 16 | FROM_LEFT_1ST_BUTTON_PRESSED, 0, MOUSE_WHEELED}},
        {.EventType = MOUSE_EVENT, .Event.MouseEvent = {position, 60u << 16 | FROM_LEFT_1ST_BUTTON_PRESSED, 0, MOUSE_WHEELED}},
        {.EventType = FOCUS_EVENT, .Event.FocusEvent = {FALSE}},
    };
    CHECK(enqueue(input, mouse, sizeof(mouse) / sizeof(*mouse)));
    OK(nt_windows_host_step(host, 105, &policy, &report));
    CHECK(fixture.down == 1 && fixture.move == 1 && fixture.wheel == 1 && fixture.cancel == 1);
    fixture.fail_reserved = true;
    CHECK(enqueue(input, &keys[2], 1));
    CHECK(nt_windows_host_step(host, 106, &policy, &report) == NT_RESULT_CALLBACK_FAILED);
    CHECK(report.cleanup_result == NT_RESULT_OK && fixture.reserved == 4);
    CHECK(GetConsoleMode(input, &mode) && mode == old_input);
    CHECK(GetConsoleMode(output, &mode) && mode == old_output);
    CHECK(GetConsoleCursorInfo(output, &cursor) && cursor.bVisible == old_cursor.bVisible &&
          cursor.dwSize == old_cursor.dwSize);
    OK(nt_windows_host_close(host));
    nt_windows_host_destroy(host);
    NtWindowsHostOptions alternate = options;
    alternate.alternate_screen = true;
    const COORD saved_position = {screen.srWindow.Left, screen.srWindow.Top};
    DWORD written;
    CHECK(WriteConsoleOutputCharacterW(output, L"S", 1, saved_position, &written) && written == 1);
    OK(nt_windows_host_open(fixture.tree, 107, NULL, &alternate, &host));
    OK(nt_windows_host_step(host, 107, NULL, &report));
    CHECK(report.resized && report.presented);
    OK(nt_windows_host_close(host));
    CHECK(ReadConsoleOutputCharacterW(output, glyphs, 1, saved_position, &read) && read == 1 && glyphs[0] == 'S');
    CHECK(GetConsoleMode(input, &mode) && mode == old_input);
    CHECK(GetConsoleMode(output, &mode) && mode == old_output);
    CHECK(GetConsoleCursorInfo(output, &cursor) && cursor.bVisible == old_cursor.bVisible &&
          cursor.dwSize == old_cursor.dwSize);
    nt_windows_host_destroy(host);
    const DWORD public_mode = old_input & ~(ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE | ENABLE_AUTO_POSITION);
    CHECK(SetConsoleMode(input, public_mode));
    OK(nt_windows_terminal_probe(input, output, &probe));
    CHECK(probe.host_available && !probe.capabilities.pointer);
    alternate.alternate_screen = false;
    alternate.capabilities = probe.capabilities;
    OK(nt_windows_host_open(fixture.tree, 108, NULL, &alternate, &host));
    CHECK(GetConsoleMode(input, &mode) && !(mode & (ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS)));
    OK(nt_windows_host_close(host));
    CHECK(GetConsoleMode(input, &mode) && mode == public_mode);
    nt_windows_host_destroy(host);
    nt_tree_destroy(fixture.tree);
    return 0;
}

int main(void)
{
    /* Use an isolated console so no test records reach the invoking shell. */
    CHECK(FreeConsole() && AllocConsole());
    const HANDLE input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    const HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(input != INVALID_HANDLE_VALUE && output != INVALID_HANDLE_VALUE);
    const int result = exercise(input, output);
    CloseHandle(output);
    CloseHandle(input);
    FreeConsole();
    return result;
}
