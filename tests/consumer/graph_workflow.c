#include <nocturne/graph.h>
#include <nocturne/widgets.h>

#include <stdio.h>

#define TEXT(s) (NtByteView){s, sizeof(s)-1}
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "workflow graph line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static NtResult dispatch_key(NtTree *tree, NtTimeUs now, uint32_t key)
{
    const NtEvent event = {.kind=NT_EVENT_KEY,.at=now,.data.key={key,0,NT_KEY_PRESS}};
    NtDispatchResult result;
    return nt_tree_dispatch(tree,&event,(NtByteView){0},NULL,&result);
}

int main(void)
{
    enum { DRAFT=1, REVIEW=2, PACK=3, SIGNOFF=4 };
    const NtGraphNode tasks[] = {{.id=PACK},{.id=DRAFT},{.id=REVIEW},{.id=SIGNOFF}};
    const NtGraphEdge dependencies[] = {{1,DRAFT,REVIEW,true},{2,REVIEW,PACK,true},
                                       {3,DRAFT,SIGNOFF,true},{4,SIGNOFF,PACK,true}};
    const NtGraphLabel labels[] = {{DRAFT,TEXT("Draft")},{REVIEW,TEXT("Review")},
                                   {PACK,TEXT("Pack")},{SIGNOFF,TEXT("Sign off")}};
    NtGraphViewOptions view = nt_graph_view_options_default();
    view.data = (NtGraphData){tasks,4,dependencies,4};
    view.labels = labels;
    view.label_count = 4;

    NtTree *tree;
    NtNodeId panel, graph;
    CHECK(nt_tree_create(NULL,&tree) == NT_RESULT_OK);
    NtPanelOptions shell = {.framed=true,.border={.foreground={185,200,228,255}},
        .fill={18,30,48,255}};
    shell.title.text = TEXT("Publication workflow");
    shell.title.style = (NtLabelStyle){.width={NT_WIDTH_ONE,NT_WIDTH_TWO},.tab_stop=4,
                                      .color=shell.border};
    CHECK(nt_panel_mount(tree,(NtNodeId){0},(NtMountOptions){true,false,true},&shell,&panel) == NT_RESULT_OK);
    CHECK(nt_graph_view_mount(tree,panel,(NtMountOptions){true,true,true},&view,&graph) == NT_RESULT_OK);
    CHECK(nt_tree_publish(tree,(NtCellSize){31,9}) == NT_RESULT_OK);
    NtGraphHit position;
    CHECK(nt_graph_view_hit(tree,graph,(NtCellPoint){1,4},&position) == NT_RESULT_OK &&
          position.kind == NT_GRAPH_HIT_NODE && position.id == DRAFT);
    CHECK(nt_graph_view_hit(tree,graph,(NtCellPoint){0,4},&position) == NT_RESULT_OK && !position.id);
    CHECK(nt_graph_view_hit(tree,graph,(NtCellPoint){15,1},&position) == NT_RESULT_OK &&
          position.kind == NT_GRAPH_HIT_NODE && position.id == REVIEW);
    CHECK(nt_tree_set_focus(tree,graph) == NT_RESULT_OK);
    CHECK(dispatch_key(tree,1,NT_KEY_RIGHT) == NT_RESULT_OK);
    CHECK(dispatch_key(tree,2,NT_KEY_RIGHT) == NT_RESULT_OK);
    NtEventIntentView intent;
    for (NtGraphId expect = DRAFT; expect <= REVIEW; ++expect) {
        CHECK(nt_tree_peek_event_intent(tree,&intent));
        CHECK(nt_graph_event_decode(intent,&position) == NT_RESULT_OK && position.id == expect);
        nt_tree_pop_intent(tree);
    }
    CHECK(!nt_tree_peek_event_intent(tree,&intent));
    CHECK(nt_graph_view_selection(tree,graph,&position) == NT_RESULT_OK && position.id == REVIEW);

    /* Simulate a desktop pane narrowing while leaving the selected task intact. */
    CHECK(nt_tree_publish(tree,(NtCellSize){19,7}) == NT_RESULT_OK);
    CHECK(nt_graph_view_hit(tree,graph,(NtCellPoint){9,1},&position) == NT_RESULT_OK &&
          position.kind == NT_GRAPH_HIT_NODE && position.id == REVIEW);
    NtCellView selected;
    CHECK(nt_frame_cell(nt_tree_frame(tree),(NtCellPoint){9,1},&selected) == NT_RESULT_OK);
    CHECK(selected.style.foreground.red == view.selected.red);
    CHECK(dispatch_key(tree,3,NT_KEY_LEFT) == NT_RESULT_OK);
    CHECK(nt_graph_view_selection(tree,graph,&position) == NT_RESULT_OK && position.id == DRAFT);
    CHECK(nt_graph_view_select(tree,graph,(NtGraphHit){NT_GRAPH_HIT_NODE,PACK}) == NT_RESULT_OK);
    CHECK(nt_graph_view_selection(tree,graph,&position) == NT_RESULT_OK && position.id == PACK);
    CHECK(nt_tree_remove(tree,panel) == NT_RESULT_OK);
    CHECK(nt_graph_view_hit(tree,graph,(NtCellPoint){9,1},&position) == NT_RESULT_STALE_REFERENCE);
    nt_tree_destroy(tree);
    return 0;
}
