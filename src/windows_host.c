#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <nocturne/windows_host.h>

#include "allocator.h"
#include "event_internal.h"
#include "input_internal.h"
#include "present_internal.h"

#include <limits.h>
#include <string.h>

#define BYTES(text) ((NtByteView){text, sizeof(text) - 1})

enum { INPUT_WORK_LIMIT = 128, VIRTUAL_KEY_COUNT = 256 };

typedef struct PendingKey {
    NtKeyInput input;
    char text[4];
    size_t text_count;
    unsigned count;
    bool text_only;
} PendingKey;

struct NtWindowsHost {
    NtAllocator allocator;
    NtTree *tree;
    NtWindowsHostOptions options;
    NtAnsiPresenter presenter;
    NtTimeUs time;
    NtCellSize size;
    SMALL_RECT window;
    DWORD prior_input_mode, prior_output_mode;
    CONSOLE_CURSOR_INFO prior_cursor;
    KEY_EVENT_RECORD high_surrogate[2];
    PendingKey pending_key;
    uint32_t keys[VIRTUAL_KEY_COUNT], pressed;
    int32_t wheel_x, wheel_y;
    bool open, busy, sized, input_changed, output_changed, alternate, cursor_changed;
    bool presentation_started;
};

NT_DECLARE_ALIGNMENT(NtWindowsHost);
NT_DECLARE_ALIGNMENT(WCHAR);

static volatile LONG owner;

static bool valid_handle(HANDLE handle)
{
    return handle && handle != INVALID_HANDLE_VALUE;
}

static bool input_console(HANDLE handle, DWORD *mode)
{
    DWORD count;
    return GetConsoleMode(handle, mode) && GetNumberOfConsoleInputEvents(handle, &count);
}

static bool output_console(HANDLE handle, DWORD *mode)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    return GetConsoleMode(handle, mode) && GetConsoleScreenBufferInfo(handle, &info);
}

NtResult nt_windows_terminal_probe(void *input_handle, void *output_handle,
                                   NtWindowsTerminalReport *out)
{
    if (!out || !valid_handle(output_handle) || (input_handle && !valid_handle(input_handle)))
        return NT_RESULT_INVALID_ARGUMENT;
    DWORD input_mode, output_mode;
    const bool input = input_handle && input_console(input_handle, &input_mode);
    const bool output = output_console(output_handle, &output_mode);
    const bool pointer = input && output && (input_mode & ENABLE_EXTENDED_FLAGS);
    *out = (NtWindowsTerminalReport){
        .capabilities = {.color = NT_COLOR_MONO, .interactive = output, .utf8 = output,
                         .pointer = pointer, .width = {NT_WIDTH_ONE, NT_WIDTH_TWO}},
        .input_console = input, .output_console = output, .host_available = input && output,
    };
    return NT_RESULT_OK;
}

static NtResult write_console(void *context, NtByteView bytes)
{
    const NtWindowsHost *host = context;
    if (!bytes.count) return NT_RESULT_OK;
    if (bytes.count > INT_MAX || bytes.count > SIZE_MAX / sizeof(WCHAR))
        return NT_RESULT_OUT_OF_RANGE;
    WCHAR local[128];
    const size_t storage = bytes.count * sizeof(WCHAR);
    WCHAR *wide = storage <= sizeof(local) ? local :
        nt_allocate(&host->allocator, storage, NT_ALIGNMENT(WCHAR));
    if (!wide) return NT_RESULT_ALLOCATION_FAILED;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data,
                                          (int)bytes.count, wide, (int)bytes.count);
    NtResult result = count ? NT_RESULT_OK : NT_RESULT_TRANSPORT_FAILED;
    for (DWORD offset = 0; offset < (DWORD)count;) {
        DWORD written = 0;
        if (!WriteConsoleW(host->options.output_handle, wide + offset, (DWORD)count - offset,
                           &written, NULL) || !written || written > (DWORD)count - offset) {
            result = NT_RESULT_TRANSPORT_FAILED;
            break;
        }
        offset += written;
    }
    if (wide != local) nt_release(&host->allocator, wide, storage, NT_ALIGNMENT(WCHAR));
    return result;
}

static NtResult start_console(NtWindowsHost *host)
{
    const NtWindowsHostOptions *options = &host->options;
    const DWORD output = (host->prior_output_mode | ENABLE_PROCESSED_OUTPUT |
                          ENABLE_VIRTUAL_TERMINAL_PROCESSING) & ~ENABLE_WRAP_AT_EOL_OUTPUT;
    if (output != host->prior_output_mode) {
        host->output_changed = true;
        if (!SetConsoleMode(options->output_handle, output)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    }
    DWORD input = host->prior_input_mode & ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT |
            ENABLE_ECHO_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT);
    input |= ENABLE_WINDOW_INPUT;
    if (options->capabilities.pointer) {
        input |= ENABLE_MOUSE_INPUT;
        /* Conhost hides the private settings if extended flags were never enabled. */
        if (input & ENABLE_EXTENDED_FLAGS) input &= ~ENABLE_QUICK_EDIT_MODE;
    }
    if (input != host->prior_input_mode) {
        host->input_changed = true;
        if (!SetConsoleMode(options->input_handle, input)) return NT_RESULT_TRANSPORT_FAILED;
    }
    if (options->alternate_screen) {
        host->alternate = true;
        const NtResult result = write_console(host, BYTES("\x1b[?1049h"));
        if (result != NT_RESULT_OK) return result;
    }
    if (options->hide_cursor) {
        host->cursor_changed = true;
        return write_console(host, BYTES("\x1b[?25l"));
    }
    return NT_RESULT_OK;
}

static NtResult restore_console(NtWindowsHost *host)
{
    NtResult result = NT_RESULT_OK;
    if (host->presentation_started && write_console(host, BYTES("\x1b[0m")) != NT_RESULT_OK)
        result = NT_RESULT_TRANSPORT_FAILED;
    if (host->alternate && write_console(host, BYTES("\x1b[?1049l")) != NT_RESULT_OK)
        result = NT_RESULT_TRANSPORT_FAILED;
    if (host->cursor_changed && !SetConsoleCursorInfo(host->options.output_handle, &host->prior_cursor))
        result = NT_RESULT_TRANSPORT_FAILED;
    if (host->input_changed && !SetConsoleMode(host->options.input_handle, host->prior_input_mode))
        result = NT_RESULT_TRANSPORT_FAILED;
    if (host->output_changed && !SetConsoleMode(host->options.output_handle, host->prior_output_mode))
        result = NT_RESULT_TRANSPORT_FAILED;
    return result;
}

NtResult nt_windows_host_open(NtTree *tree, NtTimeUs now, const NtAllocator *provided,
                              const NtWindowsHostOptions *options, NtWindowsHost **out)
{
    NtAllocator allocator;
    if (!tree || !out || !options || !nt_allocator_get(provided, &allocator) ||
        !valid_handle(options->input_handle) || !valid_handle(options->output_handle) ||
        (options->motion != NT_MOTION_FULL && options->motion != NT_MOTION_REDUCED) ||
        !nt_ansi_capabilities_valid(options->capabilities, options->palette))
        return NT_RESULT_INVALID_ARGUMENT;
    if (!options->capabilities.interactive || options->capabilities.bracketed_paste)
        return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (InterlockedCompareExchange(&owner, 1, 0)) return NT_RESULT_WRONG_PHASE;

    DWORD input, output;
    CONSOLE_CURSOR_INFO cursor = {0};
    NtResult result = NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (input_console(options->input_handle, &input) && output_console(options->output_handle, &output) &&
        (!options->hide_cursor || GetConsoleCursorInfo(options->output_handle, &cursor))) {
        NtWindowsHost *host = nt_allocate(&allocator, sizeof(*host), NT_ALIGNMENT(NtWindowsHost));
        result = NT_RESULT_ALLOCATION_FAILED;
        if (host) {
            *host = (NtWindowsHost){.allocator = allocator, .tree = tree, .options = *options,
                .time = now, .prior_input_mode = input, .prior_output_mode = output, .prior_cursor = cursor};
            host->presenter = (NtAnsiPresenter){options->capabilities, options->matte, options->palette,
                                               write_console, host};
            result = start_console(host);
            if (result == NT_RESULT_OK) {
                host->open = true;
                *out = host;
                return NT_RESULT_OK;
            }
            if (restore_console(host) != NT_RESULT_OK) result = NT_RESULT_TRANSPORT_FAILED;
            nt_release(&allocator, host, sizeof(*host), NT_ALIGNMENT(NtWindowsHost));
        }
    }
    InterlockedExchange(&owner, 0);
    return result;
}

static NtResult dispatch(NtWindowsHost *host, const NtEvent *event, NtByteView text,
                         const NtDispatchPolicy *policy, NtWindowsStep *step)
{
    NtDispatchResult delivery;
    NtResult result = nt_tree_dispatch(host->tree, event, text, policy, &delivery);
    if (result != NT_RESULT_OK) return result;
    ++step->input_events;
    if (delivery.reserved && host->options.reserved_key) {
        result = host->options.reserved_key(host->options.reserved_context, event, text);
        if (result != NT_RESULT_OK) return result;
    }
    return host->options.after_input ?
        host->options.after_input(host->options.input_context, &delivery) : NT_RESULT_OK;
}

static NtResult cancel(NtWindowsHost *host, NtCancelReason reason)
{
    const NtEvent event = {.kind = NT_EVENT_HOST_CANCEL, .at = host->time, .data.reason = reason};
    NtDispatchResult delivery;
    return nt_tree_dispatch(host->tree, &event, (NtByteView){0}, NULL, &delivery);
}

static NtResult close_host(NtWindowsHost *host)
{
    host->open = false;
    const NtResult restored = restore_console(host);
    const NtResult canceled = cancel(host, NT_CANCEL_HOST);
    InterlockedExchange(&owner, 0);
    return restored != NT_RESULT_OK ? restored : canceled;
}

NtResult nt_windows_host_close(NtWindowsHost *host)
{
    if (!host) return NT_RESULT_INVALID_ARGUMENT;
    if (host->busy) return NT_RESULT_WRONG_PHASE;
    if (!host->open) return NT_RESULT_OK;
    host->busy = true;
    const NtResult result = close_host(host);
    host->busy = false;
    return result;
}

void nt_windows_host_destroy(NtWindowsHost *host)
{
    if (!host || host->busy) return;
    (void)nt_windows_host_close(host);
    const NtAllocator allocator = host->allocator;
    nt_release(&allocator, host, sizeof(*host), NT_ALIGNMENT(NtWindowsHost));
}

static NtResult resize_console(NtWindowsHost *host, NtWindowsStep *step)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(host->options.output_handle, &info))
        return NT_RESULT_TRANSPORT_FAILED;
    host->window = info.srWindow;
    const int columns = info.srWindow.Right - info.srWindow.Left + 1;
    const int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    if (columns <= 0 || rows <= 0) return NT_RESULT_TRANSPORT_FAILED;
    const NtCellSize size = {(uint32_t)columns, (uint32_t)rows};
    if (host->sized && size.columns == host->size.columns && size.rows == host->size.rows)
        return NT_RESULT_OK;
    NtResult result = nt_tree_publish(host->tree, size);
    if (result != NT_RESULT_OK) return result;
    host->sized = true;
    host->size = step->size = size;
    step->resized = true;
    const NtEvent event = {.kind = NT_EVENT_RESIZE, .at = host->time, .data.resize = size};
    NtDispatchResult delivery;
    result = nt_tree_dispatch(host->tree, &event, (NtByteView){0}, NULL, &delivery);
    if (result == NT_RESULT_OK && host->options.after_resize)
        result = host->options.after_resize(host->options.resize_context, size);
    return result;
}

static uint32_t modifiers(DWORD state)
{
    uint32_t result = 0;
    if (state & SHIFT_PRESSED) result |= NT_MOD_SHIFT;
    if (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) result |= NT_MOD_ALT;
    if (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) result |= NT_MOD_CONTROL;
    if (state & CAPSLOCK_ON) result |= NT_MOD_CAPS_LOCK;
    if (state & NUMLOCK_ON) result |= NT_MOD_NUM_LOCK;
    return result;
}

static uint32_t named_key(WORD code)
{
    switch (code) {
        case VK_ESCAPE: return NT_KEY_ESCAPE;
        case VK_RETURN: return NT_KEY_ENTER;
        case VK_TAB: return NT_KEY_TAB;
        case VK_BACK: return NT_KEY_BACKSPACE;
        case VK_INSERT: return NT_KEY_INSERT;
        case VK_DELETE: return NT_KEY_DELETE;
        case VK_HOME: return NT_KEY_HOME;
        case VK_END: return NT_KEY_END;
        case VK_PRIOR: return NT_KEY_PAGE_UP;
        case VK_NEXT: return NT_KEY_PAGE_DOWN;
        case VK_UP: return NT_KEY_UP;
        case VK_DOWN: return NT_KEY_DOWN;
        case VK_LEFT: return NT_KEY_LEFT;
        case VK_RIGHT: return NT_KEY_RIGHT;
        default: return code >= VK_F1 && code <= VK_F12 ? NT_KEY_F1 + (code - VK_F1) : 0;
    }
}

static unsigned key_utf16(NtWindowsHost *host, const KEY_EVENT_RECORD *record, WCHAR text[2])
{
    /* Console text injection can interleave a key release after each UTF-16 unit. */
    KEY_EVENT_RECORD *pending = &host->high_surrogate[record->bKeyDown != FALSE];
    const KEY_EVENT_RECORD high = *pending;
    *pending = (KEY_EVENT_RECORD){0};
    const WCHAR code = record->uChar.UnicodeChar;
    if (code >= 0xd800 && code <= 0xdbff) {
        *pending = *record;
        return 0;
    }
    if (code >= 0xdc00 && code <= 0xdfff) {
        if (!high.uChar.UnicodeChar || high.wVirtualKeyCode != record->wVirtualKeyCode ||
            high.wVirtualScanCode != record->wVirtualScanCode ||
            high.wRepeatCount != record->wRepeatCount ||
            high.dwControlKeyState != record->dwControlKeyState) return 0;
        text[0] = high.uChar.UnicodeChar;
        text[1] = code;
        return 2;
    }
    text[0] = code;
    return 1;
}

static void queue_key(NtWindowsHost *host, const KEY_EVENT_RECORD *record)
{
    WCHAR text[2] = {0};
    const unsigned length = key_utf16(host, record, text);
    if (!length) return;
    const uint32_t scalar = length == 2 ?
        0x10000u + ((uint32_t)text[0] - 0xd800u) * 0x400u + ((uint32_t)text[1] - 0xdc00u) : text[0];
    const WORD code = record->wVirtualKeyCode;
    const bool down = record->bKeyDown != FALSE;
    const uint32_t mods = modifiers(record->dwControlKeyState);
    const bool printable = nt_input_printable(scalar);
    PendingKey *pending = &host->pending_key;

    /* A console Alt-numpad sequence commits its character on Alt release. */
    if (!down && code == VK_MENU && printable) {
        *pending = (PendingKey){.count = 1, .text_only = true};
        pending->text_count = (size_t)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            text, (int)length, pending->text, sizeof pending->text, NULL, NULL);
        if (!pending->text_count) pending->count = 0;
        return;
    }

    const uint32_t previous = code && code < VIRTUAL_KEY_COUNT ? host->keys[code] : 0;
    uint32_t key = named_key(code);
    if (!key && printable) key = scalar;
    if (!key && (mods & (NT_MOD_CONTROL | NT_MOD_ALT)) && code >= 'A' && code <= 'Z')
        key = 'a' + (code - 'A');
    if (previous && (!down || code != VK_PACKET)) key = previous;
    if (!key) return;
    if (code && code < VIRTUAL_KEY_COUNT) host->keys[code] = down ? key : 0;
    *pending = (PendingKey){
        .input = {key, mods, !down ? NT_KEY_RELEASE : previous == key ? NT_KEY_REPEAT : NT_KEY_PRESS},
        .count = down && record->wRepeatCount ? record->wRepeatCount : 1,
    };
    const DWORD raw = record->dwControlKeyState;
    const DWORD chords = raw & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED | LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
    /* AltGr is reported as right Alt with left Control by the console. */
    if (down && printable && (!chords || chords == (RIGHT_ALT_PRESSED | LEFT_CTRL_PRESSED))) {
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                              text, (int)length, pending->text, sizeof pending->text, NULL, NULL);
        pending->text_count = (size_t)count;
    }
}

static NtResult dispatch_key(NtWindowsHost *host, const NtDispatchPolicy *policy, NtWindowsStep *step)
{
    PendingKey *key = &host->pending_key;
    const NtByteView text = {key->text, key->text_count};
    --key->count;
    if (key->text_only) {
        const NtEvent event = {.kind = NT_EVENT_TEXT, .at = host->time, .data.text = text};
        return dispatch(host, &event, (NtByteView){0}, policy, step);
    }
    const NtEvent event = {.kind = NT_EVENT_KEY, .at = host->time, .data.key = key->input};
    key->input.state = NT_KEY_REPEAT;
    return dispatch(host, &event, text, policy, step);
}

static uint32_t buttons(DWORD state)
{
    uint32_t result = 0;
    if (state & FROM_LEFT_1ST_BUTTON_PRESSED) result |= NT_BUTTON_PRIMARY;
    if (state & FROM_LEFT_2ND_BUTTON_PRESSED) result |= NT_BUTTON_MIDDLE;
    if (state & RIGHTMOST_BUTTON_PRESSED) result |= NT_BUTTON_SECONDARY;
    if (state & FROM_LEFT_3RD_BUTTON_PRESSED) result |= NT_BUTTON_8;
    if (state & FROM_LEFT_4TH_BUTTON_PRESSED) result |= NT_BUTTON_9;
    return result;
}

static NtResult mouse(NtWindowsHost *host, const MOUSE_EVENT_RECORD *record,
                      const NtDispatchPolicy *policy, NtWindowsStep *step)
{
    const NtCellPoint point = {record->dwMousePosition.X - host->window.Left,
                               record->dwMousePosition.Y - host->window.Top};
    const uint32_t mods = modifiers(record->dwControlKeyState);
    const uint32_t pressed = buttons(record->dwButtonState);
    uint32_t changed = host->pressed ^ pressed;
    while (changed) {
        const uint32_t button = changed & (0u - changed);
        host->pressed ^= button;
        changed ^= button;
        const NtEventKind kind = (pressed & button) ? NT_EVENT_POINTER_DOWN : NT_EVENT_POINTER_UP;
        const NtEvent event = {.kind = kind, .at = host->time,
                               .data.pointer = {1, point, button, host->pressed, true, mods}};
        const NtResult result = dispatch(host, &event, (NtByteView){0}, policy, step);
        if (result != NT_RESULT_OK) return result;
    }
    if (record->dwEventFlags == MOUSE_MOVED) {
        const NtEvent event = {.kind = NT_EVENT_POINTER_MOVE, .at = host->time,
                               .data.pointer = {1, point, 0, pressed, true, mods}};
        return dispatch(host, &event, (NtByteView){0}, policy, step);
    }
    if (record->dwEventFlags == MOUSE_WHEELED || record->dwEventFlags == MOUSE_HWHEELED) {
        const bool horizontal = record->dwEventFlags == MOUSE_HWHEELED;
        int32_t *residue = horizontal ? &host->wheel_x : &host->wheel_y;
        const int32_t delta = (SHORT)HIWORD(record->dwButtonState);
        *residue += horizontal ? delta : -delta;
        const int32_t steps = *residue / WHEEL_DELTA;
        *residue %= WHEEL_DELTA;
        if (steps) {
            const NtEvent event = {.kind = NT_EVENT_WHEEL, .at = host->time,
                .data.wheel = {point, horizontal ? steps : 0, horizontal ? 0 : steps, mods}};
            return dispatch(host, &event, (NtByteView){0}, policy, step);
        }
    }
    return NT_RESULT_OK;
}

static NtResult read_console(NtWindowsHost *host, const NtDispatchPolicy *policy, NtWindowsStep *step)
{
    for (unsigned work = 0; work < INPUT_WORK_LIMIT; ++work) {
        NtResult result = NT_RESULT_OK;
        if (host->pending_key.count) {
            result = dispatch_key(host, policy, step);
        } else {
            DWORD available;
            const HANDLE handle = host->options.input_handle;
            if (!GetNumberOfConsoleInputEvents(handle, &available)) return NT_RESULT_TRANSPORT_FAILED;
            if (!available) return NT_RESULT_OK;
            INPUT_RECORD record;
            DWORD count;
            if (!ReadConsoleInputW(handle, &record, 1, &count) || count != 1)
                return NT_RESULT_TRANSPORT_FAILED;
            if (record.EventType == KEY_EVENT) {
                queue_key(host, &record.Event.KeyEvent);
            } else if (record.EventType == MOUSE_EVENT && host->options.capabilities.pointer) {
                result = mouse(host, &record.Event.MouseEvent, policy, step);
            } else if (record.EventType == FOCUS_EVENT && !record.Event.FocusEvent.bSetFocus) {
                memset(host->keys, 0, sizeof(host->keys));
                memset(host->high_surrogate, 0, sizeof(host->high_surrogate));
                host->pressed = 0;
                host->wheel_x = host->wheel_y = 0;
                const NtEvent event = {.kind = NT_EVENT_HOST_CANCEL, .at = host->time,
                                       .data.reason = NT_CANCEL_FOCUS_LOSS};
                result = dispatch(host, &event, (NtByteView){0}, policy, step);
            }
        }
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult drive(NtWindowsHost *host, const NtDispatchPolicy *policy, NtWindowsStep *step)
{
    NtResult result = nt_tree_tick(host->tree, host->time, host->options.motion);
    if (result == NT_RESULT_OK) result = resize_console(host, step);
    if (result == NT_RESULT_OK) result = read_console(host, policy, step);
    if (result != NT_RESULT_OK) return result;
    const bool dirty = nt_tree_pending(host->tree) != NT_INVALIDATE_NONE;
    if (dirty) {
        result = nt_tree_publish(host->tree, host->size);
        if (result != NT_RESULT_OK) return result;
    }
    if (!dirty && !step->resized) return NT_RESULT_OK;
    host->presentation_started = true;
    result = nt_ansi_present_in_place(&host->presenter, nt_tree_frame(host->tree));
    step->presented = result == NT_RESULT_OK;
    return result;
}

NtResult nt_windows_host_step(NtWindowsHost *host, NtTimeUs now, const NtDispatchPolicy *policy,
                              NtWindowsStep *out)
{
    if (!host || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (!host->open || host->busy) return NT_RESULT_WRONG_PHASE;
    if (now < host->time) return NT_RESULT_OUT_OF_RANGE;
    NtResult result = nt_key_policy_check(policy, NULL, NULL);
    if (result != NT_RESULT_OK) return result;
    host->busy = true;
    host->time = now;
    NtWindowsStep step = {.size = host->size};
    result = drive(host, policy, &step);
    if (result != NT_RESULT_OK) step.cleanup_result = close_host(host);
    host->busy = false;
    *out = step;
    return result;
}
