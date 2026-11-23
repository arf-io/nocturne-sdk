#include <nocturne/window.h>

#include "controls_internal.h"

typedef struct ContentState {
    NtAllocator allocator;
    NtPanelContentProvider provider;
} ContentState;
NT_DECLARE_ALIGNMENT(ContentState);

static NtResult measure_content(const void *value, NtMeasureContext *context,
                                NtConstraints bounds, NtCellSize *out)
{
    if (nt_measure_child_count(context)) return NT_RESULT_INVALID_ARGUMENT;
    const ContentState *state = value;
    *out = nt_control_fit(state->provider.preferred,bounds);
    return NT_RESULT_OK;
}

static NtResult update_content(void *value, NtUpdateInfo info, NtUpdateReply *reply)
{
    ContentState *state = value;
    return state->provider.update(state->provider.context,info,reply);
}

static NtResult paint_content(const void *value, NtPaintContext *paint, NtCellSize size)
{
    const ContentState *state = value;
    return state->provider.paint(state->provider.context,paint,size);
}

static NtResult event_content(void *value, NtEventContext *context, const NtEvent *event, NtEventReply *reply)
{
    ContentState *state = value;
    return state->provider.event(state->provider.context,context,event,reply);
}

static void dispose_content(void *value)
{
    ContentState *state = value;
    if (state->provider.dispose) state->provider.dispose(state->provider.context);
    const NtAllocator allocator = state->allocator;
    nt_release(&allocator,state,sizeof(*state),NT_ALIGNMENT(ContentState));
}

NtResult nt_panel_content_mount(NtTree *tree, NtNodeId parent, NtMountOptions mount,
                                 const NtPanelContentProvider *provider, NtNodeId *out)
{
    NtResult result = nt_control_validate_mount(tree,parent,out);
    if (result != NT_RESULT_OK) return result;
    if (!provider || !provider->paint) return NT_RESULT_INVALID_ARGUMENT;
    tree->busy = true;
    ContentState *state = nt_allocate(&tree->allocator,sizeof(*state),NT_ALIGNMENT(ContentState));
    tree->busy = false;
    if (!state) return NT_RESULT_ALLOCATION_FAILED;
    *state = (ContentState){tree->allocator,*provider};
    const NtComponentOps ops = {
        measure_content,nt_control_leaf,provider->update ? update_content : NULL,paint_content,
        provider->event ? event_content : NULL,dispose_content
    };
    result = nt_tree_mount(tree,parent,&ops,state,mount,out);
    if (result != NT_RESULT_OK) {
        tree->busy = true;
        nt_release(&tree->allocator,state,sizeof(*state),NT_ALIGNMENT(ContentState));
        tree->busy = false;
    }
    return result;
}
