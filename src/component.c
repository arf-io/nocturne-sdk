#include "property_internal.h"

#include <string.h>

NT_DECLARE_ALIGNMENT(NtTree);
NT_DECLARE_ALIGNMENT(NtNode);

NtResult nt_tree_create(const NtAllocator *allocator, NtTree **out)
{
    NtAllocator selected;
    if (!out || !nt_allocator_get(allocator, &selected)) return NT_RESULT_INVALID_ARGUMENT;
    NtTree *tree = nt_allocate(&selected, sizeof(*tree), NT_ALIGNMENT(NtTree));
    if (!tree) return NT_RESULT_ALLOCATION_FAILED;
    *tree = (NtTree){.allocator = selected, .root = NT_NO_SLOT, .vacancy = NT_NO_SLOT,
                    .dirty = NT_INVALIDATE_LAYOUT};
    *out = tree;
    return NT_RESULT_OK;
}

uint32_t nt_tree_preorder(const NtTree *tree, uint32_t root, uint32_t slot, bool descend)
{
    if (descend && tree->nodes[slot].first != NT_NO_SLOT) return tree->nodes[slot].first;
    while (slot != root && tree->nodes[slot].next == NT_NO_SLOT)
        slot = tree->nodes[slot].parent;
    return slot == root ? NT_NO_SLOT : tree->nodes[slot].next;
}

static void append(NtTree *tree, uint32_t parent, uint32_t slot)
{
    NtNode *node = &tree->nodes[slot];
    node->parent = parent;
    if (parent == NT_NO_SLOT) {
        tree->root = slot;
        return;
    }
    NtNode *container = &tree->nodes[parent];
    node->previous = container->last;
    if (container->last == NT_NO_SLOT) container->first = slot;
    else tree->nodes[container->last].next = slot;
    container->last = slot;
    ++container->children;
}

static void unlink(NtTree *tree, uint32_t slot)
{
    NtNode *node = &tree->nodes[slot];
    if (node->parent == NT_NO_SLOT) tree->root = NT_NO_SLOT;
    else {
        NtNode *parent = &tree->nodes[node->parent];
        if (node->previous != NT_NO_SLOT) tree->nodes[node->previous].next = node->next;
        else parent->first = node->next;
        if (node->next != NT_NO_SLOT) tree->nodes[node->next].previous = node->previous;
        else parent->last = node->previous;
        --parent->children;
    }
    node->parent = node->previous = node->next = NT_NO_SLOT;
}

static NtResult grow_nodes(NtTree *tree)
{
    const size_t limit = SIZE_MAX / sizeof(NtNode) < UINT32_MAX ?
                         SIZE_MAX / sizeof(NtNode) : UINT32_MAX;
    if (tree->capacity == limit) return NT_RESULT_OUT_OF_RANGE;
    size_t capacity = tree->capacity ? tree->capacity : 8;
    capacity = capacity > limit / 2 ? limit : capacity * 2;
    NtNode *nodes = nt_allocate(&tree->allocator, capacity * sizeof(*nodes), NT_ALIGNMENT(NtNode));
    if (!nodes) return NT_RESULT_ALLOCATION_FAILED;
    if (tree->slots) memcpy(nodes, tree->nodes, tree->slots * sizeof(*nodes));
    nt_release(&tree->allocator, tree->nodes, tree->capacity * sizeof(*nodes), NT_ALIGNMENT(NtNode));
    tree->nodes = nodes;
    tree->capacity = capacity;
    return NT_RESULT_OK;
}

NtResult nt_tree_mount(NtTree *tree, NtNodeId parent, const NtComponentOps *ops,
                       void *state, NtMountOptions options, NtNodeId *out)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (!out || !ops || !ops->measure || !ops->layout || !ops->paint)
        return NT_RESULT_INVALID_ARGUMENT;
    const uint32_t parent_slot = nt_node_null(parent) ? NT_NO_SLOT : parent.slot;
    if (!nt_node_null(parent)) {
        const NtResult result = nt_node_validate(tree, parent);
        if (result != NT_RESULT_OK) return result;
    } else if (tree->root != NT_NO_SLOT) return NT_RESULT_INVALID_ARGUMENT;

    uint32_t slot = tree->vacancy, generation = 1;
    if (slot == NT_NO_SLOT) {
        if (tree->slots == tree->capacity) {
            const NtResult result = grow_nodes(tree);
            if (result != NT_RESULT_OK) return result;
        }
        slot = (uint32_t)tree->slots++;
    } else {
        generation = tree->nodes[slot].generation + 1;
        tree->vacancy = tree->nodes[slot].next;
    }
    tree->nodes[slot] = (NtNode){.ops = *ops, .options = options, .state = state,
        .generation = generation, .first = NT_NO_SLOT, .last = NT_NO_SLOT,
        .previous = NT_NO_SLOT, .next = NT_NO_SLOT, .live = true};
    append(tree, parent_slot, slot);
    nt_tree_dirty(tree, NT_INVALIDATE_LAYOUT);
    *out = nt_node_ref(tree, slot);
    return NT_RESULT_OK;
}

NtResult nt_tree_move(NtTree *tree, NtNodeId id, NtNodeId parent)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtResult result = nt_node_validate(tree, id);
    if (result != NT_RESULT_OK) return result;
    if (nt_node_null(parent))
        return id.slot == tree->root ? NT_RESULT_OK : NT_RESULT_INVALID_ARGUMENT;
    result = nt_node_validate(tree, parent);
    if (result != NT_RESULT_OK) return result;
    if (id.slot == tree->root) return NT_RESULT_INVALID_ARGUMENT;
    for (uint32_t ancestor = parent.slot; ancestor != NT_NO_SLOT; ancestor = tree->nodes[ancestor].parent)
        if (ancestor == id.slot) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->nodes[id.slot].parent != parent.slot) {
        unlink(tree, id.slot);
        append(tree, parent.slot, id.slot);
        nt_tree_dirty(tree, NT_INVALIDATE_LAYOUT);
    }
    return NT_RESULT_OK;
}

static void retire_subtree(NtTree *tree, uint32_t root)
{
    for (uint32_t slot = root; slot != NT_NO_SLOT; slot = nt_tree_preorder(tree, root, slot, true))
        tree->nodes[slot].live = false;
    unlink(tree, root);
    nt_tree_dirty(tree, NT_INVALIDATE_LAYOUT);
    nt_tree_prune_schedule(tree);
    nt_tree_prune_input(tree);

    uint32_t slot = root;
    while (tree->nodes[slot].first != NT_NO_SLOT) slot = tree->nodes[slot].first;
    for (;;) {
        NtNode *node = &tree->nodes[slot];
        const uint32_t parent = node->parent, next = node->next;
        const uint32_t generation = node->generation;
        nt_property_release_provider(tree, node);
        if (node->ops.dispose) node->ops.dispose(node->state);
        nt_property_release_table(tree, node);
        *node = (NtNode){.generation = generation};
        if (generation != UINT32_MAX) {
            node->next = tree->vacancy;
            tree->vacancy = slot;
        }
        if (slot == root) break;
        if (next == NT_NO_SLOT) slot = parent;
        else {
            slot = next;
            while (tree->nodes[slot].first != NT_NO_SLOT) slot = tree->nodes[slot].first;
        }
    }
}

NtResult nt_tree_remove(NtTree *tree, NtNodeId id)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    const NtResult result = nt_node_validate(tree, id);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    retire_subtree(tree, id.slot);
    tree->busy = false;
    return NT_RESULT_OK;
}

void nt_tree_destroy(NtTree *tree)
{
    if (!tree || tree->busy) return;
    tree->busy = true;
    if (tree->root != NT_NO_SLOT) retire_subtree(tree, tree->root);
    while (tree->attachments) {
        NtTreeAttachment *attachment = tree->attachments;
        tree->attachments = attachment->next;
        *attachment = (NtTreeAttachment){0};
    }
    nt_tree_release_schedule(tree);
    nt_tree_release_intents(tree);
    nt_tree_release_gestures(tree);
    nt_snapshot_destroy(tree, &tree->committed);
    const NtAllocator allocator = tree->allocator;
    nt_release(&allocator, tree->nodes, tree->capacity * sizeof(*tree->nodes), NT_ALIGNMENT(NtNode));
    nt_release(&allocator, tree, sizeof(*tree), NT_ALIGNMENT(NtTree));
}

NtOwnerToken nt_node_owner(NtNodeId node)
{
    return node.tree ? (uint64_t)node.generation << 32 | node.slot : 0;
}

NtInvalidation nt_tree_pending(const NtTree *tree)
{
    return tree ? tree->dirty : NT_INVALIDATE_NONE;
}

NtResult nt_tree_invalidate(NtTree *tree, NtNodeId id, NtInvalidation flags)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (flags < NT_INVALIDATE_NONE || flags > NT_INVALIDATE_LAYOUT) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = nt_node_validate(tree, id);
    if (result == NT_RESULT_OK) nt_tree_dirty(tree, flags);
    return result;
}

static NtDeadline pending_update(const NtTree *tree, const NtNode *node)
{
    if (!node->ops.update) return (NtDeadline){0};
    return node->updated ? node->deadline : (NtDeadline){true, tree->watermark};
}

NtDeadline nt_tree_next_update(const NtTree *tree)
{
    NtDeadline next = {0};
    if (!tree) return next;
    for (size_t i = 0; i < tree->slots; ++i) {
        if (!tree->nodes[i].live) continue;
        const NtDeadline found = pending_update(tree, &tree->nodes[i]);
        if (found.scheduled && (!next.scheduled || found.at < next.at)) next = found;
    }
    return next;
}

NtResult nt_tree_tick(NtTree *tree, NtTimeUs now, NtMotionPolicy motion)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (motion != NT_MOTION_FULL && motion != NT_MOTION_REDUCED) return NT_RESULT_INVALID_ARGUMENT;
    if (now < tree->watermark) return NT_RESULT_OUT_OF_RANGE;
    tree->watermark = now;
    tree->busy = true;
    NtResult result = NT_RESULT_OK;
    for (uint32_t slot = tree->root; slot != NT_NO_SLOT;
         slot = nt_tree_preorder(tree, tree->root, slot, true)) {
        NtNode *node = &tree->nodes[slot];
        const NtDeadline pending = pending_update(tree, node);
        if (!pending.scheduled || pending.at > now) continue;
        const NtUpdateInfo info = {now, node->updated ? now - node->updated_at : 0, motion};
        NtUpdateReply reply = {0};
        result = node->ops.update(node->state, info, &reply);
        if (result != NT_RESULT_OK) break;
        if (reply.invalidate < NT_INVALIDATE_NONE || reply.invalidate > NT_INVALIDATE_LAYOUT) {
            result = NT_RESULT_INVALID_ARGUMENT;
            break;
        }
        node->updated = true;
        node->updated_at = now;
        node->deadline = reply.next.scheduled ? reply.next : (NtDeadline){0};
        nt_tree_dirty(tree, reply.invalidate);
    }
    tree->busy = false;
    return result;
}
