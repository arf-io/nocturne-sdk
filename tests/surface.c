#include <nocturne/surface.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POINT(x, y) ((NtCellPoint){x, y})
#define RECT(x, y, w, h) ((NtCellRect){{x, y}, {w, h}})
#define BYTES(s) ((NtByteView){s, sizeof(s) - 1})
#define COUNT(a) (sizeof(a) / sizeof(*(a)))
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static const NtRgba black = {0, 0, 0, 255};
static const NtRgba white = {255, 255, 255, 255};
static const NtRgba red = {255, 0, 0, 255};
static const NtCellStyle ink = {{255, 255, 255, 255}, {0}};
static const NtCellStyle tile = {{255, 255, 255, 255}, {255, 0, 0, 255}};

static NtFrame *make_frame(const NtAllocator *allocator, uint32_t columns, uint32_t rows)
{
    NtFrame *frame;
    CHECK(nt_frame_create(allocator, (NtCellSize){columns, rows}, &frame) == NT_RESULT_OK);
    return frame;
}

static NtCellView cell(const NtFrame *frame, int32_t x, int32_t y)
{
    NtCellView view;
    CHECK(nt_frame_cell(frame, POINT(x, y), &view) == NT_RESULT_OK);
    return view;
}

static NtCellBackground background(const NtFrame *frame, int32_t x, int32_t y)
{
    NtCellBackground value;
    CHECK(nt_frame_background(frame, POINT(x, y), &value) == NT_RESULT_OK);
    return value;
}

static bool matches(NtCellView a, NtByteView glyph, uint8_t columns, NtOwnerToken owner)
{
    return a.owner == owner && a.glyph_columns == columns && a.glyph.count == glyph.count &&
           (!glyph.count || memcmp(a.glyph.data, glyph.data, glyph.count) == 0);
}

static NtSurfaceLayer layer(const NtFrame *source, NtCellRect region, NtCellPoint at)
{
    return (NtSurfaceLayer){.source = source, .region = region, .position = at,
                            .opacity = 1, .owners = NT_SURFACE_OWNERS_SHARED};
}

static void nested_clip_preserves_cluster_footprint(void)
{
    static const char wide[] = "\xe7\x95\x8c";
    NtFrame *frame = make_frame(NULL, 8, 4);
    NtSurfaceView root, viewport, child;
    CHECK(nt_surface_root(frame, &root) == NT_RESULT_OK);
    CHECK(nt_surface_child(&root, RECT(1, 1, 4, 2), NULL, &viewport) == NT_RESULT_OK);
    CHECK(nt_surface_child(&viewport, RECT(-1, 0, 6, 2), NULL, &child) == NT_RESULT_OK);
    NtCellRect clip;
    CHECK(nt_surface_clip(&child, &clip) == NT_RESULT_OK && clip.origin.x == 1 &&
          clip.origin.y == 1 && clip.size.columns == 4 && clip.size.rows == 2);
    CHECK(nt_frame_set_glyph(frame, POINT(0, 1), BYTES("a"), 1, ink, 70) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(frame, POINT(5, 1), BYTES("b"), 1, ink, 71) == NT_RESULT_OK);

    CHECK(nt_surface_glyph(&child, POINT(0, 0), BYTES(wide), 2, tile, 11) == NT_RESULT_OK);
    CHECK(nt_surface_glyph(&child, POINT(4, 0), BYTES(wide), 2, tile, 13) == NT_RESULT_OK);
    CHECK(nt_surface_glyph(&child, POINT(2, 0), BYTES(wide), 2, tile, 19) == NT_RESULT_OK);
    CHECK(matches(cell(frame, 0, 1), BYTES("a"), 1, 70));
    CHECK(matches(cell(frame, 1, 1), BYTES(""), 0, 11));
    CHECK(matches(cell(frame, 2, 1), BYTES(wide), 2, 19));
    CHECK(matches(cell(frame, 3, 1), BYTES(wide), 2, 19));
    CHECK(cell(frame, 3, 1).lead.x == 2);
    CHECK(matches(cell(frame, 4, 1), BYTES(""), 0, 13));
    CHECK(matches(cell(frame, 5, 1), BYTES("b"), 1, 71));

    NtFrame *source = make_frame(NULL, 6, 2);
    for (int x = 0; x < 6; x += 2)
        CHECK(nt_frame_set_glyph(source, POINT(x, 0), BYTES(wide), 2, tile,
                                 (NtOwnerToken)(30 + x)) == NT_RESULT_OK);
    const uint8_t alpha[] = {255,255,255,0,255,255, 255,255,255,255,255,255};
    const NtSurfaceMask mask = {NT_MASK_ALPHA, {{0,0},{6,2}}, 0, alpha, 6, COUNT(alpha)};
    NtSurfaceLayer paint = layer(source, RECT(0, 0, 6, 2), POINT(-1, 0));
    paint.mask = &mask;
    CHECK(nt_surface_compose(&viewport, &paint, 1) == NT_RESULT_OK);
    CHECK(matches(cell(frame, 1, 1), BYTES(""), 0, 30));
    CHECK(background(frame, 2, 1).owner == 32 && background(frame, 3, 1).owner == 19);
    CHECK(matches(cell(frame, 2, 1), BYTES(wide), 2, 19));
    CHECK(matches(cell(frame, 3, 1), BYTES(wide), 2, 19));

    NtSurfaceView half;
    CHECK(nt_surface_child(&root, RECT(3, 1, 1, 1), NULL, &half) == NT_RESULT_OK);
    CHECK(nt_surface_glyph(&half, POINT(0, 0), BYTES("X"), 1,
                           (NtCellStyle){white, black}, 84) == NT_RESULT_OK);
    CHECK(nt_surface_erase_glyph(&half, POINT(0, 0)) == NT_RESULT_OK);
    CHECK(matches(cell(frame, 3, 1), BYTES(wide), 2, 19));
    CHECK(cell(frame, 3, 1).style.background.red == 0);
    CHECK(cell(frame, 2, 1).style.background.red == 255);
    CHECK(background(frame, 3, 1).owner == 84);
    CHECK(nt_surface_erase_glyph(&root, POINT(3, 1)) == NT_RESULT_OK);
    CHECK(matches(cell(frame, 2, 1), BYTES(""), 0, 32));
    CHECK(matches(cell(frame, 3, 1), BYTES(""), 0, 84));

    /* A layer crop can supply a continuation's background alone. */
    paint = layer(source, RECT(1, 0, 1, 1), POINT(6, 1));
    CHECK(nt_surface_compose(&root, &paint, 1) == NT_RESULT_OK);
    CHECK(matches(cell(frame, 6, 1), BYTES(""), 0, 30));
    CHECK(cell(frame, 7, 1).style.background.alpha == 0);
    nt_frame_destroy(source);
    nt_frame_destroy(frame);
}

static void shape_masks_and_resize(void)
{
    NtFrame *frame = make_frame(NULL, 5, 5);
    NtSurfaceView root, shape, inner;
    CHECK(nt_surface_root(frame, &root) == NT_RESULT_OK);
    NtSurfaceMask mask = {.kind = NT_MASK_ELLIPSE, .bounds = RECT(0, 0, 5, 5)};
    CHECK(nt_surface_child(&root, RECT(0, 0, 5, 5), &mask, &shape) == NT_RESULT_OK);
    CHECK(nt_surface_fill(&shape, RECT(0, 0, 5, 5), red, 2) == NT_RESULT_OK);
    CHECK(background(frame, 0, 0).color.alpha == 0 && background(frame, 2, 0).owner == 2);
    mask.kind = NT_MASK_DIAMOND;
    mask.bounds = RECT(0, 0, 4, 4);
    CHECK(nt_surface_child(&root, RECT(0, 0, 5, 5), &mask, &shape) == NT_RESULT_OK);
    double coverage;
    CHECK(nt_surface_coverage(&shape, POINT(1, 0), &coverage) == NT_RESULT_OK && coverage == 1);
    CHECK(nt_surface_coverage(&shape, POINT(0, 0), &coverage) == NT_RESULT_OK && coverage == 0);
    mask = (NtSurfaceMask){.kind = NT_MASK_ROUNDED_RECTANGLE, .bounds = RECT(0, 0, 5, 5),
                           .radius = 2};
    CHECK(nt_surface_child(&root, RECT(0, 0, 5, 5), &mask, &shape) == NT_RESULT_OK);
    CHECK(nt_surface_coverage(&shape, POINT(0, 0), &coverage) == NT_RESULT_OK && coverage == 0);
    CHECK(nt_surface_coverage(&shape, POINT(1, 0), &coverage) == NT_RESULT_OK && coverage == 1);

    const uint8_t samples[] = {128, 255, 0, 0, 128, 128};
    mask = (NtSurfaceMask){NT_MASK_ALPHA, {{0,0},{2,2}}, 0, samples, 4, COUNT(samples)};
    CHECK(nt_surface_child(&root, RECT(1, 1, 2, 2), &mask, &shape) == NT_RESULT_OK);
    CHECK(nt_surface_child(&shape, RECT(0, 0, 2, 2), &mask, &inner) == NT_RESULT_OK);
    CHECK(nt_surface_coverage(&inner, POINT(0, 1), &coverage) == NT_RESULT_OK &&
          fabs(coverage - (128.0/255.0)*(128.0/255.0)) < 1e-15);
    CHECK(nt_surface_glyph(&inner, POINT(0, 0), BYTES("W"), 2, ink, 44) == NT_RESULT_OK);
    CHECK(cell(frame, 1, 1).style.foreground.alpha == 64 && cell(frame, 2, 1).owner == 44);
    CHECK(nt_surface_fill(&root, RECT(1, 1, 1, 1), white, 5) == NT_RESULT_OK);
    CHECK(nt_frame_resize(frame, (NtCellSize){2, 5}) == NT_RESULT_OK);
    CHECK(matches(cell(frame, 1, 1), BYTES(""), 0, 5));
    CHECK(nt_surface_coverage(&inner, POINT(1, 0), &coverage) == NT_RESULT_OK && coverage == 0);
    CHECK(nt_frame_resize(frame, (NtCellSize){8, 5}) == NT_RESULT_OK);
    CHECK(nt_surface_coverage(&inner, POINT(1, 0), &coverage) == NT_RESULT_OK && coverage == 1);
    CHECK(nt_surface_coverage(&root, POINT(5, 0), &coverage) == NT_RESULT_OK && coverage == 0);

    mask.count = 5;
    CHECK(nt_surface_child(&root, RECT(0, 0, 2, 2), &mask, &inner) == NT_RESULT_INSUFFICIENT_BUFFER);
    CHECK(nt_surface_child(&root, RECT(0, 0, 1, 1), NULL, &root) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(nt_surface_child(&root, RECT(INT32_MAX, 0, UINT32_MAX, 1), NULL, &shape) == NT_RESULT_OK);
    CHECK(nt_surface_child(&shape, RECT(1, 0, 1, 1), NULL, &inner) == NT_RESULT_OUT_OF_RANGE);
    coverage = -1;
    CHECK(nt_surface_coverage(&shape, POINT(1, 0), &coverage) == NT_RESULT_OUT_OF_RANGE &&
          coverage == -1);
    CHECK(nt_frame_resize(frame, (NtCellSize){0, 5}) == NT_RESULT_OK);
    CHECK(nt_surface_fill(&root, RECT(0, 0, 5, 5), white, 7) == NT_RESULT_OK);
    CHECK(nt_surface_compose(&root, NULL, 0) == NT_RESULT_OK);
    CHECK(nt_surface_coverage(&root, POINT(0, 0), &coverage) == NT_RESULT_OK && coverage == 0);
    nt_frame_destroy(frame);
}

typedef struct MapContext {
    unsigned calls;
    NtOwnerToken reject;
} MapContext;

static NtResult mapped_owner(void *context, NtOwnerToken source, NtOwnerToken *out)
{
    MapContext *map = context;
    ++map->calls;
    if (source == map->reject) return NT_RESULT_OUT_OF_RANGE;
    *out = source + 100;
    return NT_RESULT_OK;
}

static void ordered_layers_and_alias(void)
{
    NtFrame *frame = make_frame(NULL, 5, 1);
    NtFrame *source = make_frame(NULL, 2, 1);
    NtSurfaceView root, offscreen;
    CHECK(nt_surface_root(frame, &root) == NT_RESULT_OK);
    CHECK(nt_surface_root(source, &offscreen) == NT_RESULT_OK);
    CHECK(nt_surface_fill(&root, RECT(0, 0, 2, 1), black, 5) == NT_RESULT_OK);
    CHECK(nt_surface_fill(&offscreen, RECT(0, 0, 1, 1), white, 12) == NT_RESULT_OK);
    CHECK(nt_surface_fill(&offscreen, RECT(1, 0, 1, 1), black, 14) == NT_RESULT_OK);

    NtSurfaceLayer layers[] = {layer(source, RECT(0, 0, 1, 1), POINT(0, 0)),
                               layer(source, RECT(1, 0, 1, 1), POINT(0, 0))};
    layers[0].opacity = layers[1].opacity = .5;
    CHECK(nt_surface_compose(&root, layers, 2) == NT_RESULT_OK);
    CHECK(background(frame, 0, 0).color.red == 137 && cell(frame, 0, 0).owner == 14);
    NtSurfaceLayer reverse[] = {layers[1], layers[0]};
    reverse[0].position.x = reverse[1].position.x = 1;
    CHECK(nt_surface_compose(&root, reverse, 2) == NT_RESULT_OK);
    CHECK(background(frame, 1, 0).color.red == 188 && cell(frame, 1, 0).owner == 12);

    CHECK(nt_frame_set_glyph(frame, POINT(1, 0), BYTES("B"), 1, ink, 51) == NT_RESULT_OK);
    CHECK(nt_surface_glyph(&offscreen, POINT(0, 0), BYTES("X"), 1, ink, 13) == NT_RESULT_OK);
    CHECK(nt_surface_glyph(&offscreen, POINT(1, 0), BYTES(" "), 1, ink, 15) == NT_RESULT_OK);
    layers[0] = layer(source, RECT(0, 0, 2, 1), POINT(1, 0));
    layers[1] = layer(frame, RECT(1, 0, 1, 1), POINT(3, 0));
    MapContext map = {0};
    layers[0].owners = NT_SURFACE_OWNERS_MAP;
    layers[0].map_owner = mapped_owner;
    layers[0].owner_context = &map;
    CHECK(nt_surface_compose(&root, layers, 2) == NT_RESULT_OK && map.calls == 4);
    CHECK(matches(cell(frame, 1, 0), BYTES("X"), 1, 113));
    CHECK(background(frame, 1, 0).owner == 112);
    CHECK(matches(cell(frame, 2, 0), BYTES(" "), 1, 115));
    CHECK(matches(cell(frame, 3, 0), BYTES("B"), 1, 51));

    layers[0].owners = NT_SURFACE_OWNERS_CLEAR;
    CHECK(nt_surface_compose(&root, layers, 1) == NT_RESULT_OK);
    CHECK(matches(cell(frame, 1, 0), BYTES("X"), 1, 0));
    CHECK(background(frame, 1, 0).owner == 0);
    nt_frame_destroy(source);
    nt_frame_destroy(frame);
}

typedef struct AllocationProbe { size_t calls, fail_at, live; } AllocationProbe;

static void *allocate(void *opaque, size_t bytes, size_t alignment)
{
    AllocationProbe *probe = opaque;
    (void)alignment;
    if (++probe->calls == probe->fail_at) return NULL;
    void *address = malloc(bytes);
    if (address) ++probe->live;
    return address;
}

static void release(void *opaque, void *address, size_t bytes, size_t alignment)
{
    AllocationProbe *probe = opaque;
    (void)bytes;
    (void)alignment;
    --probe->live;
    free(address);
}

static void rollback_and_zero_alpha(void)
{
    AllocationProbe probe = {0};
    NtAllocator allocator = {&probe, allocate, release};
    NtFrame *frame = make_frame(&allocator, 4, 1);
    NtFrame *source = make_frame(NULL, 2, 1);
    NtSurfaceView root;
    CHECK(nt_surface_root(frame, &root) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(frame, POINT(0, 0), BYTES("D"), 1, ink, 1) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(source, POINT(0, 0), BYTES("A"), 1, tile, 7) == NT_RESULT_OK);
    CHECK(nt_frame_set_glyph(source, POINT(1, 0), BYTES("B"), 1, tile, 8) == NT_RESULT_OK);
    const NtCellView old = cell(frame, 0, 0);
    const size_t live = probe.live;
    NtSurfaceLayer paint = layer(source, RECT(0, 0, 2, 1), POINT(0, 0));
    MapContext map = {0, 8};
    paint.owners = NT_SURFACE_OWNERS_MAP;
    paint.map_owner = mapped_owner;
    paint.owner_context = &map;
    CHECK(nt_surface_compose(&root, &paint, 1) == NT_RESULT_OUT_OF_RANGE && map.calls == 3);
    CHECK(cell(frame, 0, 0).glyph.data == old.glyph.data && probe.live == live);
    CHECK(background(frame, 0, 0).color.alpha == 0 && old.glyph.data[0] == 'D');

    paint.owners = NT_SURFACE_OWNERS_SHARED;
    for (size_t i = 1; i <= 4; ++i) {
        probe.fail_at = probe.calls + i;
        CHECK(nt_surface_compose(&root, &paint, 1) == NT_RESULT_ALLOCATION_FAILED);
        CHECK(cell(frame, 0, 0).glyph.data == old.glyph.data && probe.live == live);
        CHECK(background(frame, 0, 0).color.alpha == 0 && cell(frame, 1, 0).glyph.count == 0);
    }
    probe.fail_at = probe.calls + 1;
    CHECK(nt_surface_glyph(&root, POINT(0, 0), BYTES("T"), 1, tile, 3) ==
          NT_RESULT_ALLOCATION_FAILED);
    CHECK(background(frame, 0, 0).color.alpha == 0 && cell(frame, 0, 0).glyph.data == old.glyph.data);
    const size_t calls = probe.calls;
    paint.opacity = .001;
    CHECK(nt_surface_compose(&root, &paint, 1) == NT_RESULT_OK && probe.calls == calls);
    CHECK(cell(frame, 0, 0).glyph.data == old.glyph.data);
    probe.fail_at = 0;
    paint.opacity = 1;
    CHECK(nt_surface_compose(&root, &paint, 1) == NT_RESULT_OK);
    CHECK(matches(cell(frame, 0, 0), BYTES("A"), 1, 7));
    CHECK(matches(cell(frame, 1, 0), BYTES("B"), 1, 8));
    nt_frame_destroy(source);
    nt_frame_destroy(frame);
    CHECK(probe.live == 0);
}

static void exact_alpha_boundaries(void)
{
    const uint8_t outer_byte = 85, inner_byte = 5;
    const NtSurfaceMask outer = {NT_MASK_ALPHA, {{0,0},{1,1}}, 0, &outer_byte, 1, 1};
    const NtSurfaceMask inner = {NT_MASK_ALPHA, {{0,0},{1,1}}, 0, &inner_byte, 1, 1};
    const double opacities[] = {nextafter(.5, 0), .5, nextafter(.5, 1)};
    NtFrame *source = make_frame(NULL, 1, 1);
    const NtRgba sample = {70, 50, 30, 153};
    CHECK(nt_frame_set_glyph(source, POINT(0, 0), BYTES("X"), 1,
                             (NtCellStyle){sample, sample}, 7) == NT_RESULT_OK);
    for (unsigned i = 0; i < COUNT(opacities); ++i) {
        NtFrame *frame = make_frame(NULL, 1, 1);
        NtSurfaceView root, child;
        CHECK(nt_surface_root(frame, &root) == NT_RESULT_OK);
        CHECK(nt_surface_child(&root, RECT(0, 0, 1, 1), &outer, &child) == NT_RESULT_OK);
        NtSurfaceLayer paint = layer(source, RECT(0, 0, 1, 1), POINT(0, 0));
        paint.mask = &inner;
        paint.opacity = opacities[i];
        MapContext map = {0};
        paint.owners = NT_SURFACE_OWNERS_MAP;
        paint.map_owner = mapped_owner;
        paint.owner_context = &map;
        CHECK(nt_surface_compose(&child, &paint, 1) == NT_RESULT_OK);
        const NtCellView got = cell(frame, 0, 0);
        CHECK(got.style.foreground.alpha == (i != 0) && got.style.background.alpha == (i != 0));
        CHECK(got.owner == (i ? 107 : 0) && map.calls == (i ? 2 : 0));
        nt_frame_destroy(frame);
    }
    const uint8_t source_bytes[] = {51, 119}, mask_bytes[] = {155, 153};
    const uint8_t destination_bytes[] = {0, 30}, expected[] = {16, 62};
    for (unsigned i = 0; i < COUNT(expected); ++i) {
        NtFrame *frame = make_frame(NULL, 1, 1);
        NtSurfaceView root;
        const NtRgba old = {30, 50, 70, destination_bytes[i]};
        const NtRgba ink_color = {70, 50, 30, source_bytes[i]};
        CHECK(nt_frame_set_glyph(frame, POINT(0,0), BYTES("D"), 1, (NtCellStyle){old, old}, 8) ==
              NT_RESULT_OK);
        CHECK(nt_frame_set_glyph(source, POINT(0,0), BYTES("X"), 1,
                                 (NtCellStyle){ink_color, ink_color}, 7) == NT_RESULT_OK);
        CHECK(nt_surface_root(frame, &root) == NT_RESULT_OK);
        NtSurfaceLayer paint = layer(source, RECT(0, 0, 1, 1), POINT(0, 0));
        const NtSurfaceMask mask = {NT_MASK_ALPHA, {{0,0},{1,1}}, 0, &mask_bytes[i], 1, 1};
        paint.mask = &mask;
        paint.opacity = .5;
        CHECK(nt_surface_compose(&root, &paint, 1) == NT_RESULT_OK);
        const NtCellView got = cell(frame, 0, 0);
        CHECK(got.style.foreground.alpha == expected[i] && got.style.background.alpha == expected[i]);
        nt_frame_destroy(frame);
    }
    nt_frame_destroy(source);
}

static void positive_mask_underflow(void)
{
    NtFrame *frame = make_frame(NULL, 2, 1);
    NtSurfaceView chain[141];
    const uint8_t alpha[] = {255, 1};
    const NtSurfaceMask mask = {NT_MASK_ALPHA, {{0,0},{2,1}}, 0, alpha, 2, 2};
    CHECK(nt_surface_root(frame, &chain[0]) == NT_RESULT_OK);
    for (unsigned i = 1; i < COUNT(chain); ++i)
        CHECK(nt_surface_child(&chain[i-1], RECT(0, 0, 2, 1), &mask, &chain[i]) == NT_RESULT_OK);
    NtSurfaceView *view = &chain[COUNT(chain)-1];
    double coverage;
    CHECK(nt_surface_coverage(view, POINT(1, 0), &coverage) == NT_RESULT_OK);
    CHECK(coverage == nextafter(0, 1));
    CHECK(nt_frame_set_glyph(frame, POINT(0, 0), BYTES("W"), 2, ink, 5) == NT_RESULT_OK);
    CHECK(nt_surface_glyph(view, POINT(0, 0), BYTES("X"), 1, ink, 7) == NT_RESULT_OK);
    CHECK(matches(cell(frame, 0, 0), BYTES("X"), 1, 7) && !cell(frame, 1, 0).glyph.count);
    CHECK(nt_frame_set_glyph(frame, POINT(0, 0), BYTES("W"), 2, ink, 5) == NT_RESULT_OK);
    CHECK(nt_surface_erase_glyph(view, POINT(1, 0)) == NT_RESULT_OK);
    CHECK(!cell(frame, 0, 0).glyph.count && !cell(frame, 1, 0).glyph.count);
    nt_frame_destroy(frame);
}

static void linear_color_boundaries(void)
{
    NtFrame *frame = make_frame(NULL, 1, 1);
    NtFrame *source = make_frame(NULL, 1, 1);
    NtSurfaceView root;
    CHECK(nt_surface_root(frame, &root) == NT_RESULT_OK);
    NtSurfaceLayer paint = layer(source, RECT(0, 0, 1, 1), POINT(0, 0));
    paint.opacity = .5;
    for (uint8_t s = 0; s <= 10; ++s) {
        const NtRgba top = {s, s, s, 255};
        CHECK(nt_frame_set_glyph(source, POINT(0,0), BYTES("S"), 1,
                                 (NtCellStyle){top, top}, 7) == NT_RESULT_OK);
        for (uint8_t d = 0; d <= 10; ++d) {
            const NtRgba base = {d, d, d, 255};
            CHECK(nt_frame_set_glyph(frame, POINT(0,0), BYTES("D"), 1,
                                     (NtCellStyle){base, base}, 8) == NT_RESULT_OK);
            CHECK(nt_surface_compose(&root, &paint, 1) == NT_RESULT_OK);
            const NtCellView got = cell(frame, 0, 0);
            const unsigned expected = (s + d + 1u) / 2u;
            CHECK(got.style.foreground.red == expected && got.style.background.red == expected);
        }
    }
    nt_frame_destroy(source);
    nt_frame_destroy(frame);
}

int main(void)
{
    nested_clip_preserves_cluster_footprint();
    shape_masks_and_resize();
    ordered_layers_and_alias();
    rollback_and_zero_alpha();
    exact_alpha_boundaries();
    positive_mask_underflow();
    linear_color_boundaries();
    puts("Nested clips, masks, layers, wide glyph footprints, physical backgrounds and rollback passed.");
    return EXIT_SUCCESS;
}
