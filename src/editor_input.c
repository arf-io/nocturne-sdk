#include <nocturne/editor.h>

#include "allocator.h"
#include "editor_internal.h"

#include <string.h>

typedef struct NtEditorRegister {
    char *bytes;
    size_t count;
    bool occupied;
} NtEditorRegister;

struct NtEditorRegisters {
    NtAllocator allocator;
    NtEditorRegister *entries;
    size_t count;
};

NT_DECLARE_ALIGNMENT(NtEditorRegister);
NT_DECLARE_ALIGNMENT(NtEditorRegisters);

NtResult nt_editor_registers_create(const NtAllocator *provided, size_t slots, NtEditorRegisters **out)
{
    NtAllocator allocator;
    if (!out || !slots || !nt_allocator_get(provided, &allocator)) return NT_RESULT_INVALID_ARGUMENT;
    if (slots > SIZE_MAX / sizeof(NtEditorRegister)) return NT_RESULT_OUT_OF_RANGE;
    NtEditorRegisters *registers = nt_allocate(&allocator, sizeof(*registers), NT_ALIGNMENT(NtEditorRegisters));
    if (!registers) return NT_RESULT_ALLOCATION_FAILED;
    NtEditorRegister *entries = nt_allocate(&allocator, slots * sizeof(*entries), NT_ALIGNMENT(NtEditorRegister));
    if (!entries) {
        nt_release(&allocator, registers, sizeof(*registers), NT_ALIGNMENT(NtEditorRegisters));
        return NT_RESULT_ALLOCATION_FAILED;
    }
    for (size_t i = 0; i < slots; ++i) entries[i] = (NtEditorRegister){0};
    *registers = (NtEditorRegisters){allocator, entries, slots};
    *out = registers;
    return NT_RESULT_OK;
}

void nt_editor_registers_destroy(NtEditorRegisters *registers)
{
    if (!registers) return;
    const NtAllocator allocator = registers->allocator;
    for (size_t i = 0; i < registers->count; ++i) {
        const NtEditorRegister slot = registers->entries[i];
        nt_release(&allocator, slot.bytes, slot.count, 1);
    }
    nt_release(&allocator, registers->entries, registers->count * sizeof(*registers->entries),
               NT_ALIGNMENT(NtEditorRegister));
    nt_release(&allocator, registers, sizeof(*registers), NT_ALIGNMENT(NtEditorRegisters));
}

NtResult nt_editor_register_read(const NtEditorRegisters *registers, size_t slot, NtEditorRegisterValue *out)
{
    if (!registers || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (slot >= registers->count) return NT_RESULT_OUT_OF_RANGE;
    const NtEditorRegister entry = registers->entries[slot];
    *out = (NtEditorRegisterValue){{entry.bytes, entry.count}, entry.occupied};
    return NT_RESULT_OK;
}

NtResult nt_editor_register_write(NtEditorRegisters *registers, size_t slot, NtByteView text)
{
    if (!registers) return NT_RESULT_INVALID_ARGUMENT;
    if (slot >= registers->count) return NT_RESULT_OUT_OF_RANGE;
    const NtResult result = nt_utf8_validate(text, NULL);
    if (result != NT_RESULT_OK) return result;
    NtEditorRegister *entry = &registers->entries[slot];
    if (entry->occupied && entry->count == text.count &&
        (!text.count || memcmp(entry->bytes, text.data, text.count) == 0)) return NT_RESULT_OK;
    char *copy = NULL;
    if (text.count) {
        copy = nt_allocate(&registers->allocator, text.count, 1);
        if (!copy) return NT_RESULT_ALLOCATION_FAILED;
        memcpy(copy, text.data, text.count);
    }
    nt_release(&registers->allocator, entry->bytes, entry->count, 1);
    *entry = (NtEditorRegister){copy, text.count, true};
    return NT_RESULT_OK;
}

static NtResult register_action(NtEditor *editor, const NtEditorBinding *binding, NtEditorRegisters *registers)
{
    if (!registers) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    NtEditorRegisterValue slot;
    const NtResult result = nt_editor_register_read(registers, binding->slot, &slot);
    if (result != NT_RESULT_OK) return result;
    if (binding->action == NT_EDITOR_ACTION_REGISTER_PASTE)
        return slot.occupied ? nt_editor_replace_selection(editor, slot.text) : NT_RESULT_OK;
    const NtByteView selected = nt_editor_selected_text(editor);
    return selected.count ? nt_editor_register_write(registers, binding->slot, selected) : NT_RESULT_OK;
}

const NtEditorBinding *nt_editor_match_key(const NtEditorKeyMap *map, NtKeyInput key)
{
    if (!map || key.state == NT_KEY_RELEASE) return NULL;
    for (size_t i = 0; i < map->count; ++i) {
        const NtEditorBinding *binding = &map->bindings[i];
        if (binding->chord.key == key.key && binding->chord.modifiers == key.modifiers)
            return key.state != NT_KEY_REPEAT || binding->repeat ? binding : NULL;
    }
    return NULL;
}

static NtResult run_binding(NtEditor *editor, const NtEditorBinding *binding, NtEditorRegisters *registers)
{
    switch (binding->action) {
        case NT_EDITOR_ACTION_MOVE: return nt_editor_move(editor, binding->move, binding->extend);
        case NT_EDITOR_ACTION_DELETE_PREVIOUS: return nt_editor_delete(editor, true);
        case NT_EDITOR_ACTION_DELETE_NEXT: return nt_editor_delete(editor, false);
        case NT_EDITOR_ACTION_NEWLINE: return nt_editor_replace_selection(editor, (NtByteView){"\n",1});
        case NT_EDITOR_ACTION_UNDO: return nt_editor_undo(editor, NULL);
        case NT_EDITOR_ACTION_REDO: return nt_editor_redo(editor, NULL);
        case NT_EDITOR_ACTION_HISTORY_OLDER: return nt_editor_history_step(editor, true, NULL);
        case NT_EDITOR_ACTION_HISTORY_NEWER: return nt_editor_history_step(editor, false, NULL);
        case NT_EDITOR_ACTION_REGISTER_COPY:
        case NT_EDITOR_ACTION_REGISTER_PASTE: return register_action(editor, binding, registers);
    }
    return NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_editor_input(NtEditor *editor, const NtEvent *event, const NtEditorKeyMap *map,
                          bool *handled)
{
    if (!editor || !handled) return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = nt_event_validate(event);
    if (result != NT_RESULT_OK) return result;
    if (event->kind == NT_EVENT_TEXT || event->kind == NT_EVENT_PASTE) {
        result = nt_editor_replace_selection(editor, event->data.text);
        if (result == NT_RESULT_OK) *handled = true;
        return result;
    }
    if (event->kind == NT_EVENT_KEY && event->data.key.state != NT_KEY_RELEASE && map) {
        if (map->count && !map->bindings) return NT_RESULT_INVALID_ARGUMENT;
        if (map->count > SIZE_MAX / sizeof(*map->bindings)) return NT_RESULT_OUT_OF_RANGE;
        const NtEditorBinding *binding = nt_editor_match_key(map, event->data.key);
        if (binding) {
            result = run_binding(editor, binding, map->registers);
            if (result == NT_RESULT_OK) *handled = true;
            return result;
        }
    }
    *handled = false;
    return NT_RESULT_OK;
}
