#ifndef NOCTURNE_INTERNAL_WIDGETS_H
#define NOCTURNE_INTERNAL_WIDGETS_H

#include <nocturne/widgets.h>

#include "allocator.h"

extern const NtComponentOps nt_panel_ops;
NtResult nt_panel_state_create(const NtAllocator *allocator, const NtPanelOptions *options, void **out);

#endif
