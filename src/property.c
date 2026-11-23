#include "property_internal.h"

#include <nocturne/text.h>
#include <math.h>
#include <string.h>

NT_DECLARE_ALIGNMENT(NtPropertyDescriptor);
NT_DECLARE_ALIGNMENT(NtPropertyWrite);

typedef struct NtProviderProperty {
    NtPropertyInfo info;
    NtByteView text;
} NtProviderProperty;

typedef struct NtPropertyProviderTable {
    NtPropertyProvider provider;
    NtAllocator allocator;
    NtProviderProperty entries[];
} NtPropertyProviderTable;

struct NtPropertyOutput {
    const NtAllocator *allocator;
    NtValueKind kind;
    NtByteView previous;
    NtValue candidate;
    NtResult result;
    bool written, replaces_text;
};

typedef union NtPropertyProviderAlignment {
    NtPropertyProvider provider;
    NtAllocator allocator;
    NtProviderProperty entry;
} NtPropertyProviderAlignment;
NT_DECLARE_ALIGNMENT(NtPropertyProviderAlignment);

bool nt_value_kind_valid(NtValueKind kind)
{
    return kind >= NT_VALUE_BOOL && kind <= NT_VALUE_RGBA;
}

NtResult nt_value_validate(NtValue value)
{
    if (!nt_value_kind_valid(value.kind)) return NT_RESULT_INVALID_ARGUMENT;
    if (value.kind == NT_VALUE_TEXT) return nt_utf8_validate(value.as.text, NULL);
    if (value.kind == NT_VALUE_REAL && !isfinite(value.as.real)) return NT_RESULT_OUT_OF_RANGE;
    return NT_RESULT_OK;
}

void nt_property_release_table(const NtTree *tree, NtNode *node)
{
    if (node->owns_properties)
        nt_release(&tree->allocator, (void *)node->properties,
                   node->property_count * sizeof(*node->properties), NT_ALIGNMENT(NtPropertyDescriptor));
}

NtResult nt_tree_define_properties(NtTree *tree, NtNodeId node,
                                   const NtPropertyDescriptor *properties, size_t count)
{
    if (!tree || !properties || !count) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtResult result = nt_node_validate(tree, node);
    if (result != NT_RESULT_OK) return result;
    NtNode *mount = &tree->nodes[node.slot];
    if (mount->property_count) return NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX / sizeof(*properties)) return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < count; ++i) {
        const NtPropertyDescriptor *entry = &properties[i];
        if (!entry->id || !nt_value_kind_valid(entry->kind) || !entry->read ||
            (entry->prepare && !entry->finish) ||
            entry->invalidation < NT_INVALIDATE_NONE || entry->invalidation > NT_INVALIDATE_LAYOUT)
            return NT_RESULT_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j)
            if (properties[j].id == entry->id) return NT_RESULT_INVALID_ARGUMENT;
    }
    const size_t bytes = count * sizeof(*properties);
    tree->busy = true;
    NtPropertyDescriptor *copy = nt_allocate(&tree->allocator, bytes, NT_ALIGNMENT(NtPropertyDescriptor));
    if (copy) {
        memcpy(copy, properties, bytes);
        mount->properties = copy;
        mount->property_count = count;
        mount->owns_properties = true;
    }
    tree->busy = false;
    return copy ? NT_RESULT_OK : NT_RESULT_ALLOCATION_FAILED;
}

static size_t provider_bytes(size_t count)
{
    return sizeof(NtPropertyProviderTable) + count * sizeof(NtProviderProperty);
}

void nt_property_release_provider(const NtTree *tree, NtNode *node)
{
    NtPropertyProviderTable *table = node->property_provider;
    if (!table) return;
    if (table->provider.release) table->provider.release(table->provider.context);
    for (size_t i = 0; i < node->property_count; ++i) {
        const NtByteView text = table->entries[i].text;
        nt_release(&tree->allocator, (void *)text.data, text.count, 1);
    }
    nt_release(&tree->allocator, table, provider_bytes(node->property_count), NT_ALIGNMENT(NtPropertyProviderAlignment));
}

NtResult nt_tree_define_property_provider(NtTree *tree, NtNodeId node,
    const NtPropertyInfo *properties, size_t count, const NtPropertyProvider *provider)
{
    if (!tree || !properties || !count || !provider || !provider->read ||
        (provider->prepare && !provider->finish)) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    const NtResult valid = nt_node_validate(tree, node);
    if (valid != NT_RESULT_OK) return valid;
    NtNode *mount = &tree->nodes[node.slot];
    if (mount->property_count) return NT_RESULT_INVALID_ARGUMENT;
    if (count > (SIZE_MAX - sizeof(NtPropertyProviderTable)) / sizeof(NtProviderProperty))
        return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < count; ++i) {
        const NtPropertyInfo prop = properties[i];
        if (!prop.id || !nt_value_kind_valid(prop.kind) || (prop.writable && !provider->finish) ||
            prop.invalidation < NT_INVALIDATE_NONE || prop.invalidation > NT_INVALIDATE_LAYOUT)
            return NT_RESULT_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j)
            if (properties[j].id == prop.id) return NT_RESULT_INVALID_ARGUMENT;
    }
    tree->busy = true;
    NtPropertyProviderTable *table = nt_allocate(&tree->allocator, provider_bytes(count),
                                                 NT_ALIGNMENT(NtPropertyProviderAlignment));
    if (table) {
        table->provider = *provider;
        table->allocator = tree->allocator;
        for (size_t i = 0; i < count; ++i)
            table->entries[i] = (NtProviderProperty){.info=properties[i]};
        mount->property_provider = table;
        mount->property_count = count;
    }
    tree->busy = false;
    return table ? NT_RESULT_OK : NT_RESULT_ALLOCATION_FAILED;
}

static NtPropertyInfo property_info(const NtNode *mount, size_t index)
{
    if (mount->property_provider) return mount->property_provider->entries[index].info;
    const NtPropertyDescriptor *entry = &mount->properties[index];
    return (NtPropertyInfo){entry->id, entry->kind, entry->finish != NULL, entry->invalidation};
}

NtResult nt_tree_property_count(const NtTree *tree, NtNodeId node, size_t *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = nt_node_validate(tree, node);
    if (result == NT_RESULT_OK) *out = tree->nodes[node.slot].property_count;
    return result;
}

NtResult nt_tree_property_at(const NtTree *tree, NtNodeId node, size_t index, NtPropertyInfo *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    const NtResult result = nt_node_validate(tree, node);
    if (result != NT_RESULT_OK) return result;
    const NtNode *mount = &tree->nodes[node.slot];
    if (index >= mount->property_count) return NT_RESULT_OUT_OF_RANGE;
    *out = property_info(mount, index);
    return NT_RESULT_OK;
}

NtResult nt_property_lookup(const NtTree *tree, NtPropertyRef ref, bool write, NtAttachedProperty *out)
{
    const NtResult result = nt_node_validate(tree, ref.node);
    if (result != NT_RESULT_OK) return result;
    if (!ref.id) return NT_RESULT_INVALID_ARGUMENT;
    const NtNode *mount = &tree->nodes[ref.node.slot];
    for (size_t i = 0; i < mount->property_count; ++i) {
        const NtPropertyInfo info = property_info(mount, i);
        if (info.id != ref.id) continue;
        if (write && !info.writable) return NT_RESULT_UNSUPPORTED_CAPABILITY;
        NtPropertyProviderTable *table = mount->property_provider;
        *out = (NtAttachedProperty){.info=info,.state=mount->state};
        if (table) {
            out->provider = &table->provider;
            out->text = &table->entries[i].text;
            out->state = table;
        } else out->descriptor = &mount->properties[i];
        return NT_RESULT_OK;
    }
    return NT_RESULT_UNSUPPORTED_CAPABILITY;
}

NtResult nt_value_require_kind(NtValue value, NtValueKind kind)
{
    return value.kind == kind ? nt_value_validate(value) : NT_RESULT_INVALID_ARGUMENT;
}

NtResult nt_value_copy(const NtAllocator *allocator, NtValue value, NtValue *out)
{
    if (value.kind == NT_VALUE_TEXT) {
        char *bytes = NULL;
        if (value.as.text.count) {
            bytes = nt_allocate(allocator, value.as.text.count, 1);
            if (!bytes) return NT_RESULT_ALLOCATION_FAILED;
            memcpy(bytes, value.as.text.data, value.as.text.count);
        }
        value.as.text.data = bytes;
    }
    *out = value;
    return NT_RESULT_OK;
}

void nt_value_release(const NtAllocator *allocator, NtValue *value)
{
    if (value->kind == NT_VALUE_TEXT) {
        nt_release(allocator, (void *)value->as.text.data, value->as.text.count, 1);
        value->as.text = (NtByteView){0};
    }
}

NtResult nt_property_output_set(NtPropertyOutput *out, NtValue value)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    if (out->written) {
        if (out->result == NT_RESULT_OK) out->result = NT_RESULT_INVALID_ARGUMENT;
        return out->result;
    }
    out->written = true;
    out->result = nt_value_require_kind(value, out->kind);
    if (out->result != NT_RESULT_OK) return out->result;
    out->candidate = value;
    if (value.kind == NT_VALUE_TEXT) {
        const NtByteView text = value.as.text;
        if (text.count == out->previous.count &&
            (!text.count || !memcmp(text.data, out->previous.data, text.count))) {
            out->candidate.as.text = out->previous;
        } else {
            out->result = nt_value_copy(out->allocator, value, &out->candidate);
            out->replaces_text = out->result == NT_RESULT_OK;
        }
    }
    return out->result;
}

static NtResult provider_read(NtAttachedProperty property, NtValue *out)
{
    const NtPropertyProviderTable *table = property.state;
    const NtPropertyProvider *provider = property.provider;
    NtPropertyOutput output = {.allocator=&table->allocator,.kind=property.info.kind,.previous=*property.text};
    NtResult result = provider->read(provider->context, property.info.id, &output);
    if (result == NT_RESULT_OK) result = output.written ? output.result : NT_RESULT_INVALID_ARGUMENT;
    if (result == NT_RESULT_OK) {
        if (output.replaces_text) {
            nt_release(output.allocator, (void *)property.text->data, property.text->count, 1);
            *property.text = output.candidate.as.text;
        }
        *out = output.candidate;
    } else if (output.replaces_text) nt_value_release(output.allocator, &output.candidate);
    return result;
}

NtResult nt_property_read(NtAttachedProperty property, NtValue *out)
{
    if (property.provider) return provider_read(property, out);
    NtValue candidate = {.kind = NT_VALUE_RGBA + 1};
    NtResult result = property.descriptor->read(property.state, &candidate);
    if (result == NT_RESULT_OK) result = nt_value_require_kind(candidate, property.info.kind);
    if (result == NT_RESULT_OK) *out = candidate;
    return result;
}

NtResult nt_tree_property_get(NtTree *tree, NtPropertyRef ref, NtValue *out)
{
    if (!tree || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtAttachedProperty property;
    NtResult result = nt_property_lookup(tree, ref, false, &property);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    result = nt_property_read(property, out);
    tree->busy = false;
    return result;
}

NtResult nt_property_write_batch(NtTree *tree, NtPropertyWrite *writes, size_t count)
{
    size_t prepared = 0;
    NtResult result = NT_RESULT_OK;
    while (prepared < count) {
        NtPropertyWrite *write = &writes[prepared];
        const NtAttachedProperty property = write->property;
        write->prepared = NULL;
        if (property.provider) {
            if (property.provider->prepare)
                result = property.provider->prepare(property.provider->context, property.info.id,
                                                    write->value, &write->prepared);
        } else if (property.descriptor->prepare) {
            result = property.descriptor->prepare(property.state, write->value, &write->prepared);
        }
        if (result != NT_RESULT_OK) break;
        ++prepared;
    }
    const bool commit = result == NT_RESULT_OK;
    for (size_t i = 0; i < prepared; ++i) {
        NtPropertyWrite *write = &writes[commit ? i : prepared - i - 1];
        const NtAttachedProperty property = write->property;
        if (property.provider)
            property.provider->finish(property.provider->context, property.info.id, write->value, write->prepared, commit);
        else property.descriptor->finish(property.state, write->value, write->prepared, commit);
        write->prepared = NULL;
        if (commit) nt_tree_dirty(tree, property.info.invalidation);
    }
    return result;
}

NtResult nt_tree_property_set(NtTree *tree, NtPropertyRef ref, NtValue value)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    NtAttachedProperty property;
    NtResult result = nt_property_lookup(tree, ref, true, &property);
    if (result != NT_RESULT_OK) return result;
    result = nt_value_require_kind(value, property.info.kind);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    NtPropertyWrite write = {.property = property, .value = value};
    result = nt_property_write_batch(tree, &write, 1);
    tree->busy = false;
    return result;
}

NtResult nt_property_check_assignments(const NtTree *tree, const NtPropertyAssignment *assignments,
                                       size_t count, NtPropertyWrite *writes)
{
    if (count && !assignments) return NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX / sizeof(NtPropertyWrite) || count > SIZE_MAX / sizeof(*assignments))
        return NT_RESULT_OUT_OF_RANGE;
    for (size_t i = 0; i < count; ++i) {
        const NtPropertyAssignment *assignment = &assignments[i];
        for (size_t j = 0; j < i; ++j) {
            const NtPropertyRef previous = assignments[j].property;
            if (previous.id == assignment->property.id && nt_node_same(previous.node, assignment->property.node))
                return NT_RESULT_INVALID_ARGUMENT;
        }
        NtAttachedProperty property;
        NtResult result = nt_property_lookup(tree, assignment->property, true, &property);
        if (result == NT_RESULT_OK)
            result = nt_value_require_kind(assignment->value, property.info.kind);
        if (result != NT_RESULT_OK) return result;
        if (writes) writes[i].property = property;
    }
    return NT_RESULT_OK;
}

NtResult nt_property_apply_batch(NtTree *tree, const NtPropertyAssignment *assignments, size_t count)
{
    if (!count) return NT_RESULT_OK;
    if (!assignments) return NT_RESULT_INVALID_ARGUMENT;
    if (count > SIZE_MAX / sizeof(NtPropertyWrite) || count > SIZE_MAX / sizeof(*assignments))
        return NT_RESULT_OUT_OF_RANGE;
    const size_t bytes = count * sizeof(NtPropertyWrite);
    NtPropertyWrite *writes = nt_allocate(&tree->allocator, bytes, NT_ALIGNMENT(NtPropertyWrite));
    if (!writes) return NT_RESULT_ALLOCATION_FAILED;
    NtResult result = nt_property_check_assignments(tree, assignments, count, writes);
    size_t copied = 0;
    while (result == NT_RESULT_OK && copied < count) {
        result = nt_value_copy(&tree->allocator, assignments[copied].value, &writes[copied].value);
        if (result == NT_RESULT_OK) ++copied;
    }
    if (result == NT_RESULT_OK) result = nt_property_write_batch(tree, writes, count);
    for (size_t i = 0; i < copied; ++i) nt_value_release(&tree->allocator, &writes[i].value);
    nt_release(&tree->allocator, writes, bytes, NT_ALIGNMENT(NtPropertyWrite));
    return result;
}

NtResult nt_tree_properties_apply(NtTree *tree, const NtPropertyAssignment *assignments, size_t count)
{
    if (!tree) return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    tree->busy = true;
    const NtResult result = nt_property_apply_batch(tree, assignments, count);
    tree->busy = false;
    return result;
}
