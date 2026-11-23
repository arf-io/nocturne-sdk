#include <nocturne/binding.h>
#include "property_internal.h"

#include <string.h>

typedef struct BoundValue {
    NtBindingValue spec;
    size_t input;
    NtValue value;
} BoundValue;

typedef struct Visit {
    size_t parent, next;
    bool started, finished;
} Visit;

struct NtBindingGraph {
    NtAllocator allocator;
    NtTreeAttachment attachment;
    BoundValue *values;
    NtBindingTarget *targets;
    NtPropertyWrite *writes;
    size_t *dependencies, *order;
    NtValue *arguments;
    size_t value_count, target_count, edge_count, max_arity, input_count;
    bool releasing;
};

NT_DECLARE_ALIGNMENT(NtBindingGraph);
NT_DECLARE_ALIGNMENT(BoundValue);
NT_DECLARE_ALIGNMENT(NtBindingTarget);
NT_DECLARE_ALIGNMENT(NtPropertyWrite);
NT_DECLARE_ALIGNMENT(NtValue);
NT_DECLARE_ALIGNMENT(Visit);
typedef size_t NtBindingIndex;
NT_DECLARE_ALIGNMENT(NtBindingIndex);

static void release_graph(NtBindingGraph *graph)
{
    const NtAllocator allocator = graph->allocator;
    nt_release(&allocator, graph->values, graph->value_count * sizeof(*graph->values),
               NT_ALIGNMENT(BoundValue));
    nt_release(&allocator, graph->targets, graph->target_count * sizeof(*graph->targets),
               NT_ALIGNMENT(NtBindingTarget));
    nt_release(&allocator, graph->writes, graph->target_count * sizeof(*graph->writes),
               NT_ALIGNMENT(NtPropertyWrite));
    nt_release(&allocator, graph->dependencies, graph->edge_count * sizeof(*graph->dependencies),
               NT_ALIGNMENT(NtBindingIndex));
    nt_release(&allocator, graph->order, graph->value_count * sizeof(*graph->order),
               NT_ALIGNMENT(NtBindingIndex));
    nt_release(&allocator, graph->arguments, graph->max_arity * sizeof(*graph->arguments),
               NT_ALIGNMENT(NtValue));
    nt_release(&allocator, graph, sizeof(*graph), NT_ALIGNMENT(NtBindingGraph));
}

NtResult nt_binding_graph_destroy(NtBindingGraph *graph)
{
    if (!graph) return NT_RESULT_OK;
    NtTree *tree = graph->attachment.tree;
    if (graph->releasing || (tree && tree->busy)) return NT_RESULT_WRONG_PHASE;
    graph->releasing = true;
    if (tree) {
        tree->busy = true;
        NtTreeAttachment **link = &tree->attachments;
        while (*link != &graph->attachment) link = &(*link)->next;
        *link = graph->attachment.next;
    }
    release_graph(graph);
    if (tree) tree->busy = false;
    return NT_RESULT_OK;
}

static NtResult matching_property(const NtTree *tree, NtPropertyRef ref, NtValueKind kind,
                                   bool write, NtAttachedProperty *out)
{
    const NtResult result = nt_property_lookup(tree, ref, write, out);
    return result == NT_RESULT_OK && out->info.kind != kind ? NT_RESULT_INVALID_ARGUMENT : result;
}

static NtResult validate_graph(const NtTree *tree, const NtBindingValue *values,
                               const NtBindingTarget *targets, NtBindingGraph *shape)
{
    for (size_t i = 0; i < shape->value_count; ++i) {
        const NtBindingValue *value = &values[i];
        if (!nt_value_kind_valid(value->kind)) return NT_RESULT_INVALID_ARGUMENT;
        switch (value->source) {
        case NT_BINDING_INPUT:
            ++shape->input_count;
            break;
        case NT_BINDING_PROPERTY: {
            NtAttachedProperty property;
            const NtResult result = matching_property(tree, value->from.property, value->kind, false, &property);
            if (result != NT_RESULT_OK) return result;
            break;
        }
        case NT_BINDING_COMPUTED: {
            const size_t count = value->from.computed.count;
            const size_t *indices = value->from.computed.dependencies;
            if (!value->from.computed.evaluate || (count && !indices)) return NT_RESULT_INVALID_ARGUMENT;
            if (count > SIZE_MAX / sizeof(*shape->dependencies) - shape->edge_count)
                return NT_RESULT_OUT_OF_RANGE;
            if (count > SIZE_MAX / sizeof(*shape->arguments)) return NT_RESULT_OUT_OF_RANGE;
            shape->edge_count += count;
            if (count > shape->max_arity) shape->max_arity = count;
            for (size_t j = 0; j < count; ++j)
                if (indices[j] >= shape->value_count) return NT_RESULT_OUT_OF_RANGE;
            break;
        }
        default:
            return NT_RESULT_INVALID_ARGUMENT;
        }
    }
    for (size_t i = 0; i < shape->target_count; ++i) {
        const NtBindingTarget *target = &targets[i];
        if (target->value >= shape->value_count) return NT_RESULT_OUT_OF_RANGE;
        NtAttachedProperty property;
        const NtResult result = matching_property(tree, target->property, values[target->value].kind,
                                                  true, &property);
        if (result != NT_RESULT_OK) return result;
        for (size_t j = 0; j < i; ++j)
            if (targets[j].property.id == target->property.id &&
                nt_node_same(targets[j].property.node, target->property.node))
                return NT_RESULT_INVALID_ARGUMENT;
    }
    return NT_RESULT_OK;
}

/* Iterative dependency walk. A started but unfinished dependency is a cycle. */
static NtResult order_values(const NtBindingGraph *graph, Visit *visits)
{
    memset(visits, 0, graph->value_count * sizeof(*visits));
    size_t written = 0;
    for (size_t root = 0; root < graph->value_count; ++root) {
        if (visits[root].finished) continue;
        visits[root].parent = SIZE_MAX;
        size_t current = root;
        while (current != SIZE_MAX) {
            Visit *visit = &visits[current];
            const NtBindingValue *value = &graph->values[current].spec;
            visit->started = true;
            if (value->source == NT_BINDING_COMPUTED && visit->next < value->from.computed.count) {
                const size_t dependency = value->from.computed.dependencies[visit->next++];
                if (visits[dependency].finished) continue;
                if (visits[dependency].started) return NT_RESULT_INVALID_ARGUMENT;
                visits[dependency].parent = current;
                current = dependency;
            } else {
                visit->finished = true;
                graph->order[written++] = current;
                current = visit->parent;
            }
        }
    }
    return NT_RESULT_OK;
}

static NtResult provision(NtBindingGraph *graph, const NtBindingValue *values,
                          const NtBindingTarget *targets)
{
    const NtAllocator *allocator = &graph->allocator;
    graph->values = nt_allocate(allocator, graph->value_count * sizeof(*graph->values),
                                NT_ALIGNMENT(BoundValue));
    graph->targets = nt_allocate(allocator, graph->target_count * sizeof(*graph->targets),
                                 NT_ALIGNMENT(NtBindingTarget));
    graph->writes = nt_allocate(allocator, graph->target_count * sizeof(*graph->writes),
                                NT_ALIGNMENT(NtPropertyWrite));
    graph->order = nt_allocate(allocator, graph->value_count * sizeof(*graph->order),
                               NT_ALIGNMENT(NtBindingIndex));
    if (graph->edge_count)
        graph->dependencies = nt_allocate(allocator, graph->edge_count * sizeof(*graph->dependencies),
                                          NT_ALIGNMENT(NtBindingIndex));
    if (graph->max_arity)
        graph->arguments = nt_allocate(allocator, graph->max_arity * sizeof(*graph->arguments),
                                       NT_ALIGNMENT(NtValue));
    if (!graph->values || !graph->targets || !graph->writes || !graph->order ||
        (graph->edge_count && !graph->dependencies) || (graph->max_arity && !graph->arguments))
        return NT_RESULT_ALLOCATION_FAILED;

    size_t edge = 0, input = 0;
    for (size_t i = 0; i < graph->value_count; ++i) {
        graph->values[i] = (BoundValue){.spec = values[i]};
        NtBindingValue *value = &graph->values[i].spec;
        if (value->source == NT_BINDING_INPUT) graph->values[i].input = input++;
        if (value->source == NT_BINDING_COMPUTED) {
            const size_t count = value->from.computed.count;
            if (count) {
                memcpy(graph->dependencies + edge, value->from.computed.dependencies,
                       count * sizeof(*graph->dependencies));
                value->from.computed.dependencies = graph->dependencies + edge;
                edge += count;
            } else value->from.computed.dependencies = NULL;
        }
    }
    for (size_t i = 0; i < graph->target_count; ++i)
        graph->targets[i] = targets[i];
    const size_t bytes = graph->value_count * sizeof(Visit);
    Visit *visits = nt_allocate(allocator, bytes, NT_ALIGNMENT(Visit));
    if (!visits) return NT_RESULT_ALLOCATION_FAILED;
    const NtResult result = order_values(graph, visits);
    nt_release(allocator, visits, bytes, NT_ALIGNMENT(Visit));
    return result;
}

NtResult nt_binding_graph_create(NtTree *tree, const NtBindingValue *values, size_t value_count,
                                 const NtBindingTarget *targets, size_t target_count,
                                 NtBindingGraph **out)
{
    if (!tree || !values || !value_count || !targets || !target_count || !out)
        return NT_RESULT_INVALID_ARGUMENT;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (value_count > SIZE_MAX / sizeof(BoundValue) || value_count > SIZE_MAX / sizeof(Visit) ||
        value_count > SIZE_MAX / sizeof(size_t) || target_count > SIZE_MAX / sizeof(NtBindingTarget) ||
        target_count > SIZE_MAX / sizeof(NtPropertyWrite))
        return NT_RESULT_OUT_OF_RANGE;
    NtBindingGraph shape = {.allocator = tree->allocator, .attachment.tree = tree,
                            .value_count = value_count, .target_count = target_count};
    NtResult result = validate_graph(tree, values, targets, &shape);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    NtBindingGraph *graph = nt_allocate(&shape.allocator, sizeof(*graph), NT_ALIGNMENT(NtBindingGraph));
    if (graph) {
        *graph = shape;
        result = provision(graph, values, targets);
    } else result = NT_RESULT_ALLOCATION_FAILED;
    if (result == NT_RESULT_OK) {
        graph->attachment.next = tree->attachments;
        tree->attachments = &graph->attachment;
        *out = graph;
    } else if (graph) release_graph(graph);
    tree->busy = false;
    return result;
}

static NtResult preflight(NtBindingGraph *graph, const NtValue *inputs)
{
    for (size_t i = 0; i < graph->value_count; ++i) {
        BoundValue *step = &graph->values[i];
        const NtBindingValue *value = &step->spec;
        NtResult result = NT_RESULT_OK;
        if (value->source == NT_BINDING_PROPERTY) {
            NtAttachedProperty property;
            result = matching_property(graph->attachment.tree, value->from.property, value->kind, false, &property);
        } else if (value->source == NT_BINDING_INPUT) {
            result = nt_value_require_kind(inputs[step->input], value->kind);
        }
        if (result != NT_RESULT_OK) return result;
    }
    for (size_t i = 0; i < graph->target_count; ++i) {
        const NtBindingTarget *target = &graph->targets[i];
        const NtResult result = matching_property(graph->attachment.tree, target->property,
            graph->values[target->value].spec.kind, true, &graph->writes[i].property);
        if (result != NT_RESULT_OK) return result;
    }
    return NT_RESULT_OK;
}

static NtResult evaluate(NtBindingGraph *graph, const NtValue *inputs, BoundValue *step)
{
    const NtBindingValue *spec = &step->spec;
    NtValue value = {.kind = NT_VALUE_RGBA + 1};
    NtResult result = NT_RESULT_OK;
    switch (spec->source) {
    case NT_BINDING_INPUT:
        value = inputs[step->input];
        break;
    case NT_BINDING_PROPERTY: {
        NtAttachedProperty property;
        result = nt_property_lookup(graph->attachment.tree, spec->from.property, false, &property);
        if (result == NT_RESULT_OK) result = nt_property_read(property, &value);
        break;
    }
    case NT_BINDING_COMPUTED:
        for (size_t i = 0; i < spec->from.computed.count; ++i)
            graph->arguments[i] = graph->values[spec->from.computed.dependencies[i]].value;
        result = spec->from.computed.evaluate(spec->from.computed.context,
            spec->from.computed.count ? graph->arguments : NULL, spec->from.computed.count, &value);
        if (result == NT_RESULT_OK) result = nt_value_require_kind(value, spec->kind);
        break;
    }
    return result == NT_RESULT_OK ? nt_value_copy(&graph->allocator, value, &step->value) : result;
}

NtResult nt_binding_graph_apply(NtBindingGraph *graph, const NtValue *inputs, size_t count)
{
    if (!graph || (count && !inputs)) return NT_RESULT_INVALID_ARGUMENT;
    if (graph->releasing) return NT_RESULT_WRONG_PHASE;
    NtTree *tree = graph->attachment.tree;
    if (!tree) return NT_RESULT_STALE_REFERENCE;
    if (tree->busy) return NT_RESULT_WRONG_PHASE;
    if (count != graph->input_count) return NT_RESULT_INVALID_ARGUMENT;
    NtResult result = preflight(graph, inputs);
    if (result != NT_RESULT_OK) return result;
    tree->busy = true;
    for (size_t i = 0; i < graph->value_count && result == NT_RESULT_OK; ++i)
        if (graph->values[i].spec.source == NT_BINDING_INPUT)
            result = evaluate(graph, inputs, &graph->values[i]);
    for (size_t i = 0; i < graph->value_count && result == NT_RESULT_OK; ++i)
        if (graph->values[i].spec.source == NT_BINDING_PROPERTY)
            result = evaluate(graph, inputs, &graph->values[i]);
    for (size_t i = 0; i < graph->value_count && result == NT_RESULT_OK; ++i) {
        BoundValue *step = &graph->values[graph->order[i]];
        if (step->spec.source == NT_BINDING_COMPUTED) result = evaluate(graph, inputs, step);
    }
    if (result == NT_RESULT_OK) {
        for (size_t i = 0; i < graph->target_count; ++i)
            graph->writes[i].value = graph->values[graph->targets[i].value].value;
        result = nt_property_write_batch(tree, graph->writes, graph->target_count);
    }
    for (size_t i = 0; i < graph->value_count; ++i)
        nt_value_release(&graph->allocator, &graph->values[i].value);
    tree->busy = false;
    return result;
}
