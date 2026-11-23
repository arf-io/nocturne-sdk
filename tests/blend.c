#include <nocturne/blend.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "line %d: %s\n", __LINE__, #e); exit(1); } } while (0)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void near(double actual, double reference)
{
    if (!(fabs(actual - reference) <= 1e-12)) {
        fprintf(stderr, "expected %.17g, found %.17g\n", reference, actual);
        exit(1);
    }
}

static void all_blends(void)
{
    const struct { const char *name; double gray; } cases[] = {
        {"normal",.75},{"multiply",.1875},{"screen",.8125},{"overlay",.375},
        {"darken",.25},{"lighten",.75},{"color-dodge",1},{"color-burn",0},
        {"hard-light",.625},{"soft-light",.375},{"difference",.5},{"exclusion",.625},
        {"hue",.25},{"saturation",.25},{"color",.25},{"luminosity",.75},
        {"add",1},{"subtract",0},{"divide",1.0/3},{"linear-burn",0},
        {"linear-light",.75},{"vivid-light",.5},{"pin-light",.5},{"hard-mix",1},
        {"darker-color",.25},{"lighter-color",.75},{"dissolve",.75}
    };
    const NtColor b = {NT_COLOR_SRGB,{.25,.25,.25},1}, s = {NT_COLOR_SRGB,{.75,.75,.75},1};
    NtCompositeOptions spec = {.space = NT_COLOR_SRGB};
    NtColor color;
    CHECK(COUNT(cases) == NT_BLEND_DISSOLVE + 1u);
    for (unsigned i = 0; i < COUNT(cases); ++i) {
        CHECK(nt_blend_mode_parse(cases[i].name, &spec.blend) == NT_RESULT_OK &&
              spec.blend == (NtBlendMode)i);
        CHECK(nt_color_composite(b, s, &spec, &color) == NT_RESULT_OK && color.alpha == 1);
        for (unsigned j = 0; j < 3; ++j) near(color.channel[j], cases[i].gray);
    }
    CHECK(nt_blend_mode_parse("none", &spec.blend) == NT_RESULT_OK && spec.blend == NT_BLEND_NORMAL);
    CHECK(nt_blend_mode_parse("Multiply", &spec.blend) == NT_RESULT_UNSUPPORTED_CAPABILITY &&
          spec.blend == NT_BLEND_NORMAL);
    CHECK(nt_blend_mode_parse(NULL, &spec.blend) == NT_RESULT_INVALID_ARGUMENT);

    const NtColor backdrop = {NT_COLOR_SRGB,{.2,.4,.6},1};
    const NtColor source = {NT_COLOR_SRGB,{.8,.4,.1},1};
    const double expected[][3] = {
        {.5408571428571429,.3122857142857143,.14085714285714286},
        {.0785,.4285,.7785},
        {.362 + (.675-.362)*.362/.387, .362 + (.275-.362)*.362/.387,0},
        {.325,.525,.725}
    };
    for (unsigned i = 0; i < COUNT(expected); ++i) {
        spec.blend = (NtBlendMode)(NT_BLEND_HUE + i);
        CHECK(nt_color_composite(backdrop, source, &spec, &color) == NT_RESULT_OK);
        for (unsigned j = 0; j < 3; ++j) near(color.channel[j], expected[i][j]);
    }
    spec.blend = NT_BLEND_HARD_MIX;
    CHECK(nt_color_composite((NtColor){NT_COLOR_SRGB,{0,1,.7},1},
                             (NtColor){NT_COLOR_SRGB,{1,0,.23},1}, &spec, &color) == NT_RESULT_OK);
    CHECK(color.channel[0] == 1 && color.channel[1] == 1 && color.channel[2] == 0);
    CHECK(nt_color_composite((NtColor){NT_COLOR_SRGB,{nextafter(.5,0),0,0},1},
                             (NtColor){NT_COLOR_SRGB,{.5,0,0},1}, &spec, &color) == NT_RESULT_OK);
    CHECK(color.channel[0] == 0);
    for (unsigned i = 0; i <= 255; ++i) {
        NtRgba packed;
        const NtRgba d = {(uint8_t)i,0,0,255}, t = {(uint8_t)(255-i),0,0,255};
        CHECK(nt_rgba_composite(d,t,&spec,&packed) == NT_RESULT_OK && packed.red == 255);
    }
}

static void all_operators(void)
{
    const struct { const char *name; double top, bottom; } cases[] = {
        {"source-over",.6,.16},{"source",.6,0},{"destination",0,.4},{"clear",0,0},
        {"source-in",.24,0},{"source-out",.36,0},{"source-atop",.24,.16},
        {"destination-over",.36,.4},{"destination-in",0,.24},{"destination-out",0,.16},
        {"destination-atop",.36,.24},{"xor",.36,.16},{"lighter",.6,.4}
    };
    const NtColor b = {NT_COLOR_SRGB,{0,0,1},.4}, s = {NT_COLOR_SRGB,{1,0,0},.6};
    NtCompositeOptions spec = {.space = NT_COLOR_SRGB};
    NtColor color;
    CHECK(COUNT(cases) == NT_COMPOSITE_LIGHTER + 1u);
    for (unsigned i = 0; i < COUNT(cases); ++i) {
        CHECK(nt_composite_operator_parse(cases[i].name, &spec.operation) == NT_RESULT_OK &&
              spec.operation == (NtCompositeOperator)i);
        CHECK(nt_color_composite(b,s,&spec,&color) == NT_RESULT_OK);
        const double alpha = cases[i].top + cases[i].bottom;
        near(color.alpha,alpha);
        near(color.channel[0],alpha ? cases[i].top/alpha : 0);
        near(color.channel[1],0);
        near(color.channel[2],alpha ? cases[i].bottom/alpha : 0);
    }
    CHECK(nt_composite_operator_parse("source-under",&spec.operation) ==
          NT_RESULT_UNSUPPORTED_CAPABILITY && spec.operation == NT_COMPOSITE_LIGHTER);
    NtRgba out;
    const NtRgba blue = {0,0,255,255}, red = {255,0,0,255};
    CHECK(nt_rgba_composite(blue,red,&spec,&out) == NT_RESULT_OK &&
          out.red == 255 && out.blue == 255 && out.alpha == 255);
    spec.operation = NT_COMPOSITE_SOURCE_OVER;
    spec.blend = NT_BLEND_MULTIPLY;
    CHECK(nt_color_composite((NtColor){NT_COLOR_SRGB,{0,0,1},0},s,&spec,&color) == NT_RESULT_OK &&
          color.channel[0] == 1 && color.channel[2] == 0 && color.alpha == .6);
    spec.blend = (NtBlendMode)99;
    CHECK(nt_color_composite(b,s,&spec,&color) == NT_RESULT_INVALID_ARGUMENT &&
          color.channel[0] == 1 && color.alpha == .6);
}

static void gamma_and_dissolve(void)
{
    const NtRgba black = {0,0,0,255}, white = {255,255,255,128};
    NtRgba out, other;
    CHECK(nt_rgba_composite(black,white,NULL,&out) == NT_RESULT_OK &&
          out.red == 188 && out.alpha == 255);
    NtCompositeOptions spec = {.space = NT_COLOR_SRGB};
    CHECK(nt_rgba_composite(black,white,&spec,&out) == NT_RESULT_OK &&
          out.red == 128 && out.alpha == 255);
    spec.space = NT_COLOR_OKLAB;
    CHECK(nt_rgba_composite(black,white,&spec,&out) == NT_RESULT_OK &&
          out.red == 100 && out.alpha == 255);
    spec.space = NT_COLOR_OKLCH;
    CHECK(nt_rgba_composite(black,white,&spec,&out) == NT_RESULT_OK &&
          out.red == 100 && out.alpha == 255);
    spec.blend = NT_BLEND_DISSOLVE;
    spec.seed = UINT64_C(0x4aa6022ff22400dc);
    spec.space = NT_COLOR_SRGB;
    unsigned highs = 0;
    for (uint64_t i = 0; i < 4096; ++i) {
        spec.sample_index = i;
        CHECK(nt_rgba_composite(black,white,&spec,&out) == NT_RESULT_OK &&
              nt_rgba_composite(black,white,&spec,&other) == NT_RESULT_OK);
        CHECK(out.alpha == 255 && out.red == other.red && (out.red == 0 || out.red == 255));
        highs += out.red == 255;
    }
    CHECK(highs > 1800 && highs < 2300);
}

static void exact_boundaries(void)
{
    const NtColor green = {NT_COLOR_SRGB,{0,11.0/64,0},1};
    const NtColor blue = {NT_COLOR_SRGB,{0,0,59.0/64},1};
    NtCompositeOptions spec = {.space=NT_COLOR_SRGB,.blend=NT_BLEND_LIGHTER_COLOR};
    NtColor got;
    CHECK(nt_color_composite(green,blue,&spec,&got) == NT_RESULT_OK &&
          got.channel[1] == green.channel[1] && got.channel[2] == 0);
    spec.blend = NT_BLEND_DARKER_COLOR;
    CHECK(nt_color_composite(blue,green,&spec,&got) == NT_RESULT_OK &&
          got.channel[1] == 0 && got.channel[2] == blue.channel[2]);

    spec.blend = NT_BLEND_COLOR_DODGE;
    const NtRgba backdrop = {210,210,210,255}, source = {3,3,3,255};
    NtRgba color;
    CHECK(nt_rgba_composite(backdrop,source,&spec,&color) == NT_RESULT_OK &&
          color.red == 213 && color.green == 213 && color.blue == 213);

    const double tiny = nextafter(0,1);
    spec.blend = NT_BLEND_NORMAL;
    const NtColor tiny_blue = {NT_COLOR_SRGB,{0,0,1},tiny};
    for (double alpha=.25; alpha<=.75; alpha+=.25) {
        const NtColor red = {NT_COLOR_SRGB,{1,0,0},alpha};
        spec.operation = NT_COMPOSITE_SOURCE_ATOP;
        CHECK(nt_color_composite(tiny_blue,red,&spec,&got) == NT_RESULT_OK &&
              got.alpha == tiny && got.channel[0] == alpha && got.channel[2] == 1-alpha);
        spec.operation = NT_COMPOSITE_DESTINATION_ATOP;
        CHECK(nt_color_composite(red,tiny_blue,&spec,&got) == NT_RESULT_OK &&
              got.alpha == tiny && got.channel[0] == alpha && got.channel[2] == 1-alpha);
    }
}

int main(void)
{
    all_blends();
    all_operators();
    gamma_and_dissolve();
    exact_boundaries();
    puts("All blend and Porter-Duff names, independent numeric fixtures, gamma and dissolve passed.");
    return EXIT_SUCCESS;
}
