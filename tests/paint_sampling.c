#include <nocturne/paint_sample.h>

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static const NtColorInterpolation linear = {
    NT_COLOR_LINEAR_SRGB, NT_GAMUT_REJECT, NT_HUE_SHORTER, NT_ALPHA_PREMULTIPLIED
};

static void near(double actual, double expected, double tolerance)
{
    if (!(fabs(actual - expected) <= tolerance)) {
        fprintf(stderr, "got %.17g, expected %.17g (tolerance %.3g)\n",
                actual, expected, tolerance);
        exit(EXIT_FAILURE);
    }
}

static NtColor color(NtColorSpace space, double a, double b, double c, double alpha)
{
    return (NtColor){space, {a, b, c, 0}, alpha};
}

static NtColor conversion(NtColor source, NtColorSpace space)
{
    NtColor result;
    CHECK(nt_color_convert(source, space, NT_GAMUT_REJECT, &result) == NT_RESULT_OK);
    CHECK(result.alpha == source.alpha && result.space == space);
    return result;
}

static bool same_rgba(NtRgba a, NtRgba b)
{
    return a.red == b.red && a.green == b.green && a.blue == b.blue && a.alpha == b.alpha;
}

static void conversions(void)
{
    const double exact = 1e-12;
    NtColor result = conversion(color(NT_COLOR_SRGB, .04045, .5, 1, .25), NT_COLOR_LINEAR_SRGB);
    near(result.channel[0], .0031308049535603713, exact);
    near(result.channel[1], .21404114048223255, exact);
    CHECK(result.channel[2] == 1);
    result = conversion(color(NT_COLOR_HSL, -300, 1, .5, 1), NT_COLOR_SRGB);
    CHECK(result.channel[0] == 1 && result.channel[1] == 1 && result.channel[2] == 0);
    result = conversion(color(NT_COLOR_HSV, 480, .75, .8, 1), NT_COLOR_HSL);
    near(result.channel[0], 120, exact);
    near(result.channel[1], .6, exact);
    near(result.channel[2], .5, exact);
    result = conversion((NtColor){NT_COLOR_CMYK, {.25, 0, 1, .2}, 1}, NT_COLOR_SRGB);
    near(result.channel[0], .6, exact);
    near(result.channel[1], .8, exact);
    CHECK(result.channel[2] == 0);
    result = conversion(result, NT_COLOR_CMYK);
    near(result.channel[0], .25, exact);
    CHECK(result.channel[1] == 0 && result.channel[2] == 1);
    near(result.channel[3], .2, exact);
    result = conversion(color(NT_COLOR_SRGB, 0, 0, 0, .25), NT_COLOR_CMYK);
    CHECK(result.channel[0] == 0 && result.channel[1] == 0 && result.channel[2] == 0 &&
          result.channel[3] == 1);

    const double primary_lab[3][3] = {
        {.6279553606, .2248630611, .1258462985},
        {.8664396115, -.2338875742, .1794984799},
        {.4520137184, -.0324569842, -.3115281477}
    };
    for (size_t i = 0; i < 3; ++i) {
        NtColor primary = {NT_COLOR_LINEAR_SRGB, {0}, 1};
        primary.channel[i] = 1;
        result = conversion(primary, NT_COLOR_OKLAB);
        for (size_t j = 0; j < 3; ++j) near(result.channel[j], primary_lab[i][j], 5e-10);
        result = conversion(conversion(result, NT_COLOR_OKLCH), NT_COLOR_LINEAR_SRGB);
        for (size_t j = 0; j < 3; ++j) near(result.channel[j], primary.channel[j], 2e-7);
    }
    result = conversion(color(NT_COLOR_LINEAR_SRGB, .125, .125, .125, 1), NT_COLOR_OKLCH);
    near(result.channel[0], .5, exact);
    CHECK(result.channel[1] == 0 && result.channel[2] == 0);
    result = conversion(color(NT_COLOR_OKLAB, .5, -1e-7, 0, 1), NT_COLOR_OKLCH);
    CHECK(result.channel[1] == 1e-7 && result.channel[2] == 180);
    result = conversion(result, NT_COLOR_OKLAB);
    near(result.channel[1], -1e-7, 1e-20);
    near(result.channel[2], 0, 1e-20);

    NtRgba bytes;
    CHECK(nt_color_to_rgba(color(NT_COLOR_SRGB, .5, 0, 1, .5), NT_GAMUT_REJECT, &bytes) ==
          NT_RESULT_OK && same_rgba(bytes, (NtRgba){128, 0, 255, 128}));
    CHECK(nt_color_to_rgba(color(NT_COLOR_LINEAR_SRGB, .5, 0, 1, .5), NT_GAMUT_REJECT, &bytes) ==
          NT_RESULT_OK && same_rgba(bytes, (NtRgba){188, 0, 255, 128}));
    for (unsigned i = 0; i <= 255; ++i) {
        NtRgba sample = {(uint8_t)i, (uint8_t)(255 - i), (uint8_t)(i ^ 53u), (uint8_t)i};
        NtColor rgb = nt_color_from_rgba(sample);
        CHECK(nt_color_to_rgba(rgb, NT_GAMUT_REJECT, &bytes) == NT_RESULT_OK &&
              same_rgba(bytes, sample));
        CHECK(nt_color_to_rgba(conversion(rgb, NT_COLOR_LINEAR_SRGB), NT_GAMUT_REJECT, &bytes) ==
              NT_RESULT_OK && same_rgba(bytes, sample));
    }

    const NtColor wide = color(NT_COLOR_OKLCH, .65, .4, 40, .25);
    result = color(NT_COLOR_HSL, 123, .2, .4, .3);
    CHECK(nt_color_convert(wide, NT_COLOR_LINEAR_SRGB, NT_GAMUT_REJECT, &result) ==
          NT_RESULT_OUT_OF_RANGE && result.space == NT_COLOR_HSL && result.channel[0] == 123);
    CHECK(nt_color_convert(wide, NT_COLOR_LINEAR_SRGB, NT_GAMUT_CLIP, &result) == NT_RESULT_OK);
    CHECK(result.channel[0] == 1 && result.channel[1] == 0 && result.channel[2] == 0 &&
          result.alpha == .25);
    CHECK(nt_color_convert(wide, NT_COLOR_LINEAR_SRGB, NT_GAMUT_REDUCE_CHROMA, &result) ==
          NT_RESULT_OK);
    result = conversion(result, NT_COLOR_OKLCH);
    near(result.channel[0], .65, 3e-6);
    near(result.channel[2], 40, 2e-4);
    CHECK(result.channel[1] > .18 && result.channel[1] < .23);
    for (unsigned i = 0; i <= 1; ++i) {
        NtColor neutral;
        CHECK(nt_color_convert(color(NT_COLOR_OKLCH, i, .2, 45, 1), NT_COLOR_LINEAR_SRGB,
                               NT_GAMUT_REDUCE_CHROMA, &neutral) == NT_RESULT_OK);
        CHECK(neutral.channel[0] == i && neutral.channel[1] == i && neutral.channel[2] == i);
    }
    CHECK(nt_color_convert(color(NT_COLOR_OKLAB, .5, DBL_MAX, DBL_MAX, 1),
                           NT_COLOR_LINEAR_SRGB, NT_GAMUT_CLIP, &result) == NT_RESULT_OUT_OF_RANGE);
    CHECK(nt_color_convert(color(NT_COLOR_SRGB, NAN, 0, 0, 1),
                           NT_COLOR_SRGB, NT_GAMUT_CLIP, &result) == NT_RESULT_INVALID_ARGUMENT);
    CHECK(result.space == NT_COLOR_OKLCH);
}

static void interpolation(void)
{
    const NtColor red = color(NT_COLOR_SRGB, 1, 0, 0, 0);
    const NtColor blue = color(NT_COLOR_SRGB, 0, 0, 1, 1);
    NtColor result;
    CHECK(nt_color_mix(red, blue, .5, linear, &result) == NT_RESULT_OK);
    CHECK(result.channel[0] == 0 && result.channel[2] == 1 && result.alpha == .5);
    CHECK(nt_color_mix(red, blue, 0, linear, &result) == NT_RESULT_OK);
    CHECK(result.channel[0] == 1 && result.alpha == 0);
    CHECK(nt_color_mix(red, color(NT_COLOR_SRGB, 0, 1, 0, 0), .5, linear, &result) == NT_RESULT_OK);
    CHECK(result.channel[0] == 0 && result.channel[1] == 0 && result.channel[2] == 0 && result.alpha == 0);
    NtColorInterpolation spec = linear;
    spec.alpha = NT_ALPHA_STRAIGHT;
    CHECK(nt_color_mix(red, blue, .5, spec, &result) == NT_RESULT_OK);
    CHECK(result.channel[0] == .5 && result.channel[2] == .5 && result.alpha == .5);

    const double smallest = nextafter(0.0, 1.0);
    CHECK(nt_color_mix(color(NT_COLOR_LINEAR_SRGB, 0, 1, 0, smallest),
                       color(NT_COLOR_LINEAR_SRGB, 0, 0, 1, smallest),
                       .5, linear, &result) == NT_RESULT_OK);
    CHECK(result.channel[1] == .5 && result.channel[2] == .5 && result.alpha == smallest);
    const NtColor huge = color(NT_COLOR_OKLAB, .5, DBL_MAX, -DBL_MAX, 1);
    spec.space = NT_COLOR_OKLAB;
    CHECK(nt_color_mix(huge, huge, .5, spec, &result) == NT_RESULT_OK &&
          result.channel[1] == DBL_MAX && result.channel[2] == -DBL_MAX);

    spec.space = NT_COLOR_HSL;
    const NtColor a = color(NT_COLOR_HSL, -10, 1, .5, .25);
    const NtColor b = color(NT_COLOR_HSL, 10, 1, .5, .75);
    const double midpoint[] = {0, 180, 0, 180};
    for (size_t i = 0; i < sizeof(midpoint) / sizeof(*midpoint); ++i) {
        spec.hue = (NtHueInterpolation)i;
        CHECK(nt_color_mix(a, b, .5, spec, &result) == NT_RESULT_OK);
        CHECK(result.channel[0] == midpoint[i] && result.alpha == .5);
    }
    spec.hue = NT_HUE_LONGER;
    CHECK(nt_color_mix(b, b, .25, spec, &result) == NT_RESULT_OK && result.channel[0] == 280);
    spec.hue = NT_HUE_SHORTER;
    CHECK(nt_color_mix(b, color(NT_COLOR_HSL, 190, 1, .5, .75),
                       .5, spec, &result) == NT_RESULT_OK && result.channel[0] == 100);
    spec.alpha = NT_ALPHA_PREMULTIPLIED;
    CHECK(nt_color_mix(a, b, .5, spec, &result) == NT_RESULT_OK && result.channel[0] == 0);
    CHECK(nt_color_mix(color(NT_COLOR_HSL, 210, 0, .5, 1), b,
                       .5, spec, &result) == NT_RESULT_OK && result.channel[0] == 10);
    CHECK(nt_color_mix(color(NT_COLOR_HSL, 210, 1, .5, 0), b,
                       .5, spec, &result) == NT_RESULT_OK && result.channel[0] == 10);
    CHECK(nt_color_mix(color(NT_COLOR_HSL, 20, 1, .5, smallest),
                       color(NT_COLOR_HSL, 100, 1, .5, 1),
                       .75, spec, &result) == NT_RESULT_OK);
    CHECK(result.channel[0] == 80 && result.alpha == .75);
    spec.space = NT_COLOR_OKLCH;
    CHECK(nt_color_mix(color(NT_COLOR_OKLCH, .6, 1e-8, 45, 1),
                       color(NT_COLOR_OKLCH, .6, .1, 100, 1),
                       .5, spec, &result) == NT_RESULT_OK && result.channel[2] == 100);
    CHECK(nt_color_mix(a, b, INFINITY, spec, &result) == NT_RESULT_INVALID_ARGUMENT &&
          result.space == NT_COLOR_OKLCH);
}

static void ramps(void)
{
    const NtRgba black = {0, 0, 0, 255}, white = {255, 255, 255, 255};
    const NtRgba colors[] = {black, white, white, {255, 0, 0, 0}, {0, 255, 0, 0}};
    const NtPaletteView palette = {colors, sizeof(colors) / sizeof(*colors)};
    NtRgba bytes;
    size_t nearest = SIZE_MAX;
    CHECK(nt_palette_lookup(palette, 1, &bytes) == NT_RESULT_OK && same_rgba(bytes, white));
    CHECK(nt_palette_lookup(palette, palette.count, &bytes) == NT_RESULT_OUT_OF_RANGE &&
          same_rgba(bytes, white));
    CHECK(nt_palette_nearest(palette, nt_color_from_rgba(white), NT_GAMUT_REJECT, &nearest) ==
          NT_RESULT_OK && nearest == 1);
    CHECK(nt_palette_nearest(palette, color(NT_COLOR_SRGB, 0, 0, 1, 0), NT_GAMUT_REJECT, &nearest) ==
          NT_RESULT_OK && nearest == 3);
    CHECK(nt_palette_nearest((NtPaletteView){colors, SIZE_MAX}, nt_color_from_rgba(white),
                             NT_GAMUT_REJECT, &nearest) == NT_RESULT_OUT_OF_RANGE && nearest == 3);
    const NtRgba alpha_tie[] = {{0, 0, 0, 252}, {0, 0, 0, 3}};
    CHECK(nt_palette_nearest((NtPaletteView){alpha_tie, 2}, color(NT_COLOR_SRGB, 0, 0, 0, .5),
                             NT_GAMUT_REJECT, &nearest) == NT_RESULT_OK && nearest == 0);

    const NtColor low = nt_color_from_rgba(black), high = nt_color_from_rgba(white);
    const NtGradientStop stops[] = {
        {.1, low, NT_EASE_IN}, {.5, high, NT_LINEAR}, {.5, low, NT_STEP_END}, {.9, high, NT_LINEAR}
    };
    NtGradient gradient = {stops, 4, NT_PAINT_PAD, linear};
    NtColor result;
    CHECK(nt_gradient_sample(&gradient, -5, &result) == NT_RESULT_OK && result.channel[0] == 0);
    CHECK(nt_gradient_sample(&gradient, .3, &result) == NT_RESULT_OK);
    near(result.channel[0], .25, 1e-12);
    CHECK(nt_gradient_sample(&gradient, nextafter(.5, 0), &result) == NT_RESULT_OK &&
          result.channel[0] > .99999999);
    CHECK(nt_gradient_sample(&gradient, .5, &result) == NT_RESULT_OK && result.channel[0] == 0);
    CHECK(nt_gradient_sample(&gradient, .8, &result) == NT_RESULT_OK && result.channel[0] == 0);
    CHECK(nt_gradient_sample(&gradient, 5, &result) == NT_RESULT_OK && result.channel[0] == 1);
    gradient.address = NT_PAINT_REPEAT;
    CHECK(nt_gradient_sample(&gradient, -.7, &result) == NT_RESULT_OK);
    near(result.channel[0], .25, 1e-12);
    gradient.address = NT_PAINT_REFLECT;
    CHECK(nt_gradient_sample(&gradient, -1.7, &result) == NT_RESULT_OK);
    near(result.channel[0], .25, 1e-12);
    NtGradientStop bad_stops[] = {{0, low, NT_LINEAR}, {1, high, (NtEasing)-1}};
    gradient.stops = bad_stops;
    gradient.count = 2;
    CHECK(nt_gradient_sample(&gradient, 0, &result) == NT_RESULT_INVALID_ARGUMENT &&
          result.channel[0] > .249 && result.channel[0] < .251);

    NtPaletteRamp ramp = {{colors, 2}, NT_PALETTE_LINEAR, NT_PAINT_PAD, linear, NT_LINEAR};
    CHECK(nt_palette_sample(&ramp, .5, &result) == NT_RESULT_OK && result.channel[0] == .5);
    ramp.address = NT_PAINT_REPEAT;
    CHECK(nt_palette_sample(&ramp, -.25, &result) == NT_RESULT_OK && result.channel[0] == .5);
    CHECK(nt_palette_sample(&ramp, 1, &result) == NT_RESULT_OK && result.channel[0] == 0);
    ramp.filter = NT_PALETTE_STEP;
    CHECK(nt_palette_sample(&ramp, .499, &result) == NT_RESULT_OK && result.channel[0] == 0);
    CHECK(nt_palette_sample(&ramp, .5, &result) == NT_RESULT_OK && result.channel[0] == 1);
    ramp.address = NT_PAINT_REFLECT;
    CHECK(nt_palette_sample(&ramp, 1.25, &result) == NT_RESULT_OK && result.channel[0] == 1);
    ramp.palette.count = 1;
    CHECK(nt_palette_sample(&ramp, 40, &result) == NT_RESULT_OK && result.channel[0] == 0);

    const NtRgba bins[] = {black, white, black, white, black, white};
    ramp.palette = (NtPaletteView){bins, 6};
    ramp.address = NT_PAINT_PAD;
    const double split = 5.0 / 6.0;
    CHECK(nt_palette_sample(&ramp, nextafter(split, 0), &result) == NT_RESULT_OK &&
          result.channel[0] == 0);
    CHECK(nt_palette_sample(&ramp, split, &result) == NT_RESULT_OK && result.channel[0] == 1);
    ramp.address = NT_PAINT_REPEAT;
    ramp.filter = NT_PALETTE_LINEAR;
    ramp.easing = NT_STEP_START;
    CHECK(nt_palette_sample(&ramp, split, &result) == NT_RESULT_OK && result.channel[0] == 1);
    CHECK(nt_palette_sample(&ramp, nextafter(split, 1), &result) == NT_RESULT_OK &&
          result.channel[0] == 0);
}

static void fields(void)
{
    NtPaintField field = {.scale_x = 1, .scale_y = 1, .wave_amplitude = .5};
    NtPaintSample sample = {.x = .25, .y = .25, .motion = NT_MOTION_FULL};
    const double expected[] = {.25, .3535533905932738, .125, .75, .9489233003918702,
                               1.6730125135097733, .2785533905932738, 2.0/3, 1, .5, 0, 0};
    double coordinate;
    for (size_t i = 0; i < sizeof(expected) / sizeof(*expected); ++i) {
        field.pattern = (NtPaintPattern)i;
        CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK);
        near(coordinate, expected[i], 1e-12);
    }
    field.pattern = NT_PAINT_PLASMA;
    field.phase = .125;
    CHECK(nt_paint_coordinate(&field, (NtPaintSample){0x1p54, 0, 0, NT_MOTION_FULL},
                              &coordinate) == NT_RESULT_OK);
    near(coordinate, .125, 1e-12);
    field.phase = 0;
    field.pattern = NT_PAINT_LINEAR;
    field.angle_degrees = -1e-16;
    CHECK(nt_paint_coordinate(&field, (NtPaintSample){0, 1, 0, NT_MOTION_FULL},
                              &coordinate) == NT_RESULT_OK);
    near(coordinate, -1.7453292519943296e-18, 1e-31);
    field.pattern = NT_PAINT_CHECKER;
    sample.x = 0;
    sample.y = -1;
    field.angle_degrees = 90;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK && coordinate == 1);
    field.angle_degrees = 0;
    field.rotation = NT_PAINT_SWING;
    field.rotation_period_us = 400;
    field.swing_degrees = 90;
    sample.elapsed_us = 200;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK && coordinate == 1);
    field.rotation = NT_PAINT_FIXED;
    field.angle_degrees = 0;
    sample.x = -.25;
    sample.y = .25;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK && coordinate == 1);
    field.pattern = NT_PAINT_CHEVRONS;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK && coordinate == 1);

    field = (NtPaintField){.pattern = NT_PAINT_LINEAR, .origin_x = 4, .origin_y = 6,
                          .scale_x = 2, .scale_y = 3, .rotation = NT_PAINT_SPIN,
                          .rotation_period_us = 400, .swing_degrees = 90};
    sample = (NtPaintSample){4, 7, 100, NT_MOTION_FULL};
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK && coordinate == .5);
    sample.elapsed_us = 500;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK && coordinate == .5);
    field.reverse_rotation = true;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK && coordinate == -.5);
    field.rotation = NT_PAINT_SWING;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK && coordinate == -.5);
    sample.motion = NT_MOTION_REDUCED;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK && coordinate == 0);
    field.scale_y = 0;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_INVALID_ARGUMENT &&
          coordinate == 0);
    field.scale_y = 1;
    field.origin_x = -DBL_MAX;
    sample.x = DBL_MAX;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OUT_OF_RANGE &&
          coordinate == 0);
}

static void paint(void)
{
    const NtTween phase = {.from = 0, .to = 1, .duration_us = 400, .easing = NT_LINEAR, .repeat = NT_LOOP};
    const NtKeyframe keys[] = {{0, .25, NT_LINEAR}, {400, .75, NT_LINEAR}};
    const NtKeyframeTrack track = {keys, 2, 0, NT_ONCE};
    const NtRgba colors[] = {{0, 0, 0, 255}, {255, 255, 255, 255}};
    NtPaint paint = {
        .kind = NT_PAINT_PALETTE,
        .field = {.pattern = NT_PAINT_UNIFORM, .scale_x = 1, .scale_y = 1, .phase_track = &phase},
        .content.palette = {{colors, 2}, NT_PALETTE_LINEAR, NT_PAINT_PAD, linear, NT_LINEAR},
        .opacity = .5,
        .opacity_track = &track
    };
    NtPaintSample sample = {10, 20, 200, NT_MOTION_FULL};
    NtRgba bytes;
    CHECK(nt_paint_sample_rgba(&paint, sample, NT_GAMUT_REJECT, &bytes) == NT_RESULT_OK &&
          same_rgba(bytes, (NtRgba){188, 188, 188, 64}));
    sample.motion = NT_MOTION_REDUCED;
    CHECK(nt_paint_sample_rgba(&paint, sample, NT_GAMUT_REJECT, &bytes) == NT_RESULT_OK &&
          same_rgba(bytes, (NtRgba){0, 0, 0, 32}));
    paint.kind = NT_PAINT_SOLID;
    paint.content.solid = color(NT_COLOR_SRGB, 1, .5, 0, .5);
    CHECK(nt_paint_sample_rgba(&paint, sample, NT_GAMUT_REJECT, &bytes) == NT_RESULT_OK &&
          same_rgba(bytes, (NtRgba){255, 128, 0, 16}));
    const NtKeyframe invalid_keys[] = {{0, .25, NT_LINEAR}, {400, 2, NT_LINEAR}};
    const NtKeyframeTrack invalid = {invalid_keys, 2, 0, NT_ONCE};
    paint.opacity_track = &invalid;
    CHECK(nt_paint_sample_rgba(&paint, sample, NT_GAMUT_REJECT, &bytes) == NT_RESULT_INVALID_ARGUMENT &&
          same_rgba(bytes, (NtRgba){255, 128, 0, 16}));
}

static void checker(NtPaintField field, NtPaintSample sample, double expected)
{
    double coordinate;
    CHECK(nt_paint_coordinate(&field, sample, &coordinate) == NT_RESULT_OK);
    CHECK(coordinate == expected);
}

static void rotation_boundaries(void)
{
    NtPaintField field = {.pattern = NT_PAINT_CHECKER, .scale_x = 1, .scale_y = 1,
                          .angle_degrees = -150, .rotation = NT_PAINT_SPIN,
                          .rotation_period_us = 3};
    NtPaintSample sample = {-1, 0, 2, NT_MOTION_FULL};
    checker(field, sample, 1);

    field.rotation_period_us = UINT64_C(3) << 62;
    sample.elapsed_us = UINT64_C(2) << 62;
    checker(field, sample, 1);
    --sample.elapsed_us;
    checker(field, sample, 0);
    sample.elapsed_us += 2;
    sample.x = 1;
    checker(field, sample, 0);

    field.rotation_period_us = 3;
    field.angle_degrees = -30;
    sample.elapsed_us = 1;
    checker(field, sample, 1);
    field.angle_degrees = nextafter(-30, 0);
    checker(field, sample, 0);
    field.angle_degrees = nextafter(-30, -INFINITY);
    sample.x = -1;
    checker(field, sample, 0);

    const NtTimeUs tick = (UINT64_C(1) << 53) + 1;
    field.angle_degrees = -.5;
    field.rotation_period_us = 720 * tick;
    checker(field, (NtPaintSample){0, 1, tick, NT_MOTION_FULL}, 1);
    field.angle_degrees = -0x1.642cp+5;
    field.rotation_period_us = UINT64_C(15414383575434977280);
    checker(field, (NtPaintSample){-1, 0, UINT64_C(5759904886925369250), NT_MOTION_FULL}, 1);

    field.angle_degrees = 45;
    field.rotation_period_us = UINT64_C(1) << 63;
    checker(field, (NtPaintSample){-1, 0, UINT64_C(1) << 60, NT_MOTION_FULL}, 1);
    checker(field, (NtPaintSample){-1, 0, (UINT64_C(1) << 60) - 1, NT_MOTION_FULL}, 0);

    field.angle_degrees = 0;
    field.rotation = NT_PAINT_SWING;
    field.rotation_period_us = 12;
    field.swing_degrees = 180;
    const NtTimeUs twelfths[] = {1, 5, 7, 11};
    for (size_t i = 0; i < sizeof(twelfths) / sizeof(*twelfths); ++i) {
        sample.elapsed_us = twelfths[i];
        checker(field, sample, 1);
    }
    field.rotation_period_us = UINT64_C(1) << 63;
    field.swing_degrees = 90;
    sample = (NtPaintSample){0, -1, UINT64_C(1) << 62, NT_MOTION_FULL};
    checker(field, sample, 1);
    --sample.elapsed_us;
    checker(field, sample, 0);
    sample.motion = NT_MOTION_REDUCED;
    checker(field, sample, 1);
}

int main(void)
{
    conversions();
    interpolation();
    ramps();
    fields();
    rotation_boundaries();
    paint();
    puts("Color conversions, gamut, interpolation, palettes, 12 fields and explicit paint time");
    return 0;
}
