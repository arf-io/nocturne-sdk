#ifndef NOCTURNE_EDITOR_INTERNAL_H
#define NOCTURNE_EDITOR_INTERNAL_H

#include <nocturne/editor.h>

bool nt_editor_is_read_only(const NtEditor *editor);
uint64_t nt_editor_change_serial(const NtEditor *editor);
const NtEditorBinding *nt_editor_match_key(const NtEditorKeyMap *map, NtKeyInput key);

#endif
