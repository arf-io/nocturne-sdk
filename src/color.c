/* sRGB and Oklab formulas also appear in the legacy color_spaces.c.
 * Oklab matrices follow Björn Ottosson's published D65 transforms. */
#include "color_internal.h"

typedef struct Components { double x, y, z; } Components;

static const double radians_per_degree = 0.017453292519943295769;
static const double gamut_epsilon = 2e-7;

static double unit_clip(double value)
{
    return value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
}

static bool color_valid(NtColor color)
{
    if (!nt_color_space_valid(color.space) || !nt_unit_interval(color.alpha)) return false;
    const double a = color.channel[0], b = color.channel[1], c = color.channel[2];
    switch (color.space) {
    case NT_COLOR_SRGB:
    case NT_COLOR_LINEAR_SRGB:
        return nt_unit_interval(a) && nt_unit_interval(b) && nt_unit_interval(c);
    case NT_COLOR_HSL:
    case NT_COLOR_HSV:
        return isfinite(a) && nt_unit_interval(b) && nt_unit_interval(c);
    case NT_COLOR_OKLAB:
        return nt_unit_interval(a) && isfinite(b) && isfinite(c);
    case NT_COLOR_OKLCH:
        return nt_unit_interval(a) && isfinite(b) && b >= 0.0 && isfinite(c);
    case NT_COLOR_CMYK:
        return nt_unit_interval(a) && nt_unit_interval(b) && nt_unit_interval(c) &&
               nt_unit_interval(color.channel[3]);
    }
    return false;
}

static NtColor triple(NtColorSpace space, Components c, double alpha)
{
    return (NtColor){space, {c.x, c.y, c.z, 0.0}, alpha};
}

static Components hue_rgb(double hue, double chroma, double minimum)
{
    const double sector = hue / 60.0;
    const double x = chroma * (1.0 - fabs(fmod(sector, 2.0) - 1.0));
    Components c;
    switch ((int)sector) {
    case 0: c = (Components){chroma, x, 0}; break;
    case 1: c = (Components){x, chroma, 0}; break;
    case 2: c = (Components){0, chroma, x}; break;
    case 3: c = (Components){0, x, chroma}; break;
    case 4: c = (Components){x, 0, chroma}; break;
    default: c = (Components){chroma, 0, x}; break;
    }
    return (Components){unit_clip(c.x + minimum), unit_clip(c.y + minimum), unit_clip(c.z + minimum)};
}

static Components to_encoded(NtColor source)
{
    const double a = source.channel[0], b = source.channel[1], c = source.channel[2];
    if (source.space == NT_COLOR_HSL) {
        const double chroma = 2.0 * fmin(c, 1.0 - c) * b;
        return hue_rgb(nt_wrap_positive(a, 360.0), chroma, c - chroma / 2.0);
    }
    if (source.space == NT_COLOR_HSV) {
        const double chroma = c * b;
        return hue_rgb(nt_wrap_positive(a, 360.0), chroma, c - chroma);
    }
    if (source.space == NT_COLOR_CMYK) {
        const double k = 1.0 - source.channel[3];
        return (Components){(1.0 - a) * k, (1.0 - b) * k, (1.0 - c) * k};
    }
    return (Components){a, b, c};
}

static NtColor from_encoded(Components rgb, NtColorSpace target, double alpha)
{
    if (target == NT_COLOR_SRGB) return triple(target, rgb, alpha);
    const double largest = fmax(rgb.x, fmax(rgb.y, rgb.z));
    const double smallest = fmin(rgb.x, fmin(rgb.y, rgb.z));
    if (target == NT_COLOR_CMYK) {
        if (largest == 0.0) return (NtColor){target, {0, 0, 0, 1}, alpha};
        return (NtColor){target, {(largest - rgb.x) / largest, (largest - rgb.y) / largest,
                                   (largest - rgb.z) / largest, 1.0 - largest}, alpha};
    }
    const double delta = largest - smallest;
    const double lightness = smallest + delta / 2.0;
    if (delta == 0.0)
        return triple(target, (Components){0, 0, largest}, alpha);
    double sector;
    if (largest == rgb.x)
        sector = (rgb.y - rgb.z) / delta;
    else if (largest == rgb.y)
        sector = 2.0 + (rgb.z - rgb.x) / delta;
    else
        sector = 4.0 + (rgb.x - rgb.y) / delta;
    const double hue = nt_wrap_positive(sector * 60.0, 360.0);
    if (target == NT_COLOR_HSV)
        return triple(target, (Components){hue, delta / largest, largest}, alpha);
    const double denominator = lightness <= 0.5 ? largest + smallest :
                                                    (1.0 - largest) + (1.0 - smallest);
    return triple(target, (Components){hue, unit_clip(delta / denominator), lightness}, alpha);
}

static Components linear_to_lab(Components c)
{
    if (c.x == c.y && c.y == c.z) return (Components){cbrt(c.x), 0, 0};
    const double l = cbrt(0.4122214708*c.x + 0.5363325363*c.y + 0.0514459929*c.z);
    const double m = cbrt(0.2119034982*c.x + 0.6806995451*c.y + 0.1073969566*c.z);
    const double s = cbrt(0.0883024619*c.x + 0.2817188376*c.y + 0.6299787005*c.z);
    return (Components){
        unit_clip(0.2104542553*l + 0.7936177850*m - 0.0040720468*s),
        1.9779984951*l - 2.4285922050*m + 0.4505937099*s,
        0.0259040371*l + 0.7827717662*m - 0.8086757660*s
    };
}

static Components lab_to_linear(Components c)
{
    if (c.y == 0.0 && c.z == 0.0) {
        const double gray = c.x * c.x * c.x;
        return (Components){gray, gray, gray};
    }
    double l = c.x + 0.3963377774*c.y + 0.2158037573*c.z;
    double m = c.x - 0.1055613458*c.y - 0.0638541728*c.z;
    double s = c.x - 0.0894841775*c.y - 1.2914855480*c.z;
    l = l*l*l;
    m = m*m*m;
    s = s*s*s;
    return (Components){
         4.0767416621*l - 3.3077115913*m + 0.2309699292*s,
        -1.2684380046*l + 2.6097574011*m - 0.3413193965*s,
        -0.0041960863*l - 0.7034186147*m + 1.7076147010*s
    };
}

static bool in_gamut(Components c, double tolerance)
{
    return c.x >= -tolerance && c.x <= 1.0 + tolerance &&
           c.y >= -tolerance && c.y <= 1.0 + tolerance &&
           c.z >= -tolerance && c.z <= 1.0 + tolerance;
}

static bool finite_components(Components c)
{
    return isfinite(c.x) && isfinite(c.y) && isfinite(c.z);
}

static NtResult gamut_map(Components lab, NtGamutPolicy gamut, Components *out)
{
    Components c = lab_to_linear(lab);
    if (!finite_components(c)) return NT_RESULT_OUT_OF_RANGE;
    if (gamut == NT_GAMUT_REJECT && !in_gamut(c, gamut_epsilon)) return NT_RESULT_OUT_OF_RANGE;
    if (gamut == NT_GAMUT_REDUCE_CHROMA && !in_gamut(c, 0.0)) {
        c = lab_to_linear((Components){lab.x, 0, 0});
        if (lab.x > 0.0 && lab.x < 1.0) {
            /* Valid sRGB chroma fits in this bracket. An enormous
             * upper bound would lose useful precision after 40 steps. */
            const double scale = fmax(1.0, fmax(fabs(lab.y), fabs(lab.z)));
            lab.y /= scale;
            lab.z /= scale;
            double low = 0.0, high = 1.0;
            for (unsigned i = 0; i < 40; ++i) {
                const double mid = (low + high) * 0.5;
                const Components candidate = lab_to_linear(
                    (Components){lab.x, lab.y * mid, lab.z * mid});
                if (in_gamut(candidate, 0.0)) {
                    low = mid;
                    c = candidate;
                } else {
                    high = mid;
                }
            }
        }
    }
    *out = (Components){unit_clip(c.x), unit_clip(c.y), unit_clip(c.z)};
    return NT_RESULT_OK;
}

static NtColor canonical(NtColor source)
{
    if (source.space == NT_COLOR_HSL || source.space == NT_COLOR_HSV)
        source.channel[0] = nt_wrap_positive(source.channel[0], 360.0);
    if (source.space == NT_COLOR_OKLCH)
        source.channel[2] = nt_wrap_positive(source.channel[2], 360.0);
    if (source.space != NT_COLOR_CMYK) source.channel[3] = 0.0;
    return source;
}

NtResult nt_color_convert(NtColor source, NtColorSpace target, NtGamutPolicy gamut,
                          NtColor *out)
{
    if (!out || !nt_color_space_valid(target) || !nt_gamut_valid(gamut) || !color_valid(source))
        return NT_RESULT_INVALID_ARGUMENT;
    source = canonical(source);
    if (source.space == target) {
        *out = source;
        return NT_RESULT_OK;
    }
    const bool source_ok = source.space == NT_COLOR_OKLAB || source.space == NT_COLOR_OKLCH;
    const bool target_ok = target == NT_COLOR_OKLAB || target == NT_COLOR_OKLCH;
    Components linear = {0};
    Components lab = {source.channel[0], source.channel[1], source.channel[2]};
    if (source.space == NT_COLOR_OKLCH) {
        const double angle = lab.z * radians_per_degree;
        const double chroma = lab.y;
        lab.y = chroma * cos(angle);
        lab.z = chroma * sin(angle);
    } else if (!source_ok) {
        const Components encoded = to_encoded(source);
        if (!target_ok && target != NT_COLOR_LINEAR_SRGB && source.space != NT_COLOR_LINEAR_SRGB) {
            *out = from_encoded(encoded, target, source.alpha);
            return NT_RESULT_OK;
        }
        linear = source.space == NT_COLOR_LINEAR_SRGB ? encoded :
            (Components){nt_srgb_decode(encoded.x), nt_srgb_decode(encoded.y), nt_srgb_decode(encoded.z)};
        if (target_ok) lab = linear_to_lab(linear);
    }
    if (target_ok) {
        if (target == NT_COLOR_OKLCH) {
            const double chroma = hypot(lab.y, lab.z);
            if (!isfinite(chroma)) return NT_RESULT_OUT_OF_RANGE;
            const double hue = chroma == 0.0 ? 0.0 :
                               nt_wrap_positive(atan2(lab.z, lab.y) / radians_per_degree, 360.0);
            lab = (Components){lab.x, chroma, hue};
        }
        *out = triple(target, lab, source.alpha);
        return NT_RESULT_OK;
    }
    if (source_ok) {
        const NtResult status = gamut_map(lab, gamut, &linear);
        if (status != NT_RESULT_OK) return status;
    }
    if (target == NT_COLOR_LINEAR_SRGB) {
        *out = triple(target, linear, source.alpha);
    } else {
        const Components encoded = {nt_srgb_encode(linear.x), nt_srgb_encode(linear.y), nt_srgb_encode(linear.z)};
        *out = from_encoded(encoded, target, source.alpha);
    }
    return NT_RESULT_OK;
}

NtColor nt_color_from_rgba(NtRgba rgba)
{
    return (NtColor){NT_COLOR_SRGB, {rgba.red / 255.0, rgba.green / 255.0, rgba.blue / 255.0, 0.0},
                     rgba.alpha / 255.0};
}

static uint8_t byte_channel(double channel)
{
    return (uint8_t)round(unit_clip(channel) * 255.0);
}

NtResult nt_color_to_rgba(NtColor source, NtGamutPolicy gamut, NtRgba *out)
{
    if (!out) return NT_RESULT_INVALID_ARGUMENT;
    NtColor rgb;
    const NtResult status = nt_color_convert(source, NT_COLOR_SRGB, gamut, &rgb);
    if (status != NT_RESULT_OK) return status;
    *out = (NtRgba){byte_channel(rgb.channel[0]), byte_channel(rgb.channel[1]),
                    byte_channel(rgb.channel[2]), byte_channel(rgb.alpha)};
    return NT_RESULT_OK;
}

static bool powerless(NtColor c)
{
    if (c.space == NT_COLOR_OKLCH) return c.channel[1] <= 1e-7;
    return c.channel[1] == 0.0 || c.channel[2] == 0.0 ||
           (c.space == NT_COLOR_HSL && c.channel[2] == 1.0);
}

static double hue_delta(double difference, NtHueInterpolation policy)
{
    switch (policy) {
    case NT_HUE_SHORTER:
        return difference > 180.0 ? difference - 360.0 :
               difference < -180.0 ? difference + 360.0 : difference;
    case NT_HUE_LONGER:
        return difference >= 0.0 && difference < 180.0 ? difference - 360.0 :
               difference > -180.0 && difference < 0.0 ? difference + 360.0 : difference;
    case NT_HUE_INCREASING: return difference < 0.0 ? difference + 360.0 : difference;
    case NT_HUE_DECREASING: return difference > 0.0 ? difference - 360.0 : difference;
    }
    return difference;
}

static double weighted(double a, double b, double weight_a, double weight_b)
{
    if ((a >= 0.0 && b >= 0.0) || (a <= 0.0 && b <= 0.0))
        return weight_a < weight_b ? b + (a - b) * weight_a : a + (b - a) * weight_b;
    return a * weight_a + b * weight_b;
}

NtResult nt_color_mix(NtColor a, NtColor b, double progress,
                      NtColorInterpolation spec, NtColor *out)
{
    if (!out || !nt_unit_interval(progress) || !nt_color_interpolation_valid(spec) ||
        !color_valid(a) || !color_valid(b)) return NT_RESULT_INVALID_ARGUMENT;
    if (progress == 0.0 || progress == 1.0)
        return nt_color_convert(progress == 0.0 ? a : b, spec.space, spec.gamut, out);

    NtColor first, second;
    NtResult status = nt_color_convert(a, spec.space, spec.gamut, &first);
    if (status != NT_RESULT_OK) return status;
    status = nt_color_convert(b, spec.space, spec.gamut, &second);
    if (status != NT_RESULT_OK) return status;

    const bool premultiplied = spec.alpha == NT_ALPHA_PREMULTIPLIED;
    const double opacity = weighted(first.alpha, second.alpha, 1.0 - progress, progress);
    NtColor result = {spec.space, {0}, unit_clip(opacity)};
    if (premultiplied && opacity == 0.0) {
        *out = result;
        return NT_RESULT_OK;
    }
    double weight_a = 1.0 - progress, weight_b = progress;
    if (premultiplied) {
        const double largest_alpha = fmax(first.alpha, second.alpha);
        const double a_weighted = weight_a * (first.alpha / largest_alpha);
        const double b_weighted = weight_b * (second.alpha / largest_alpha);
        const double total = a_weighted + b_weighted;
        weight_a = a_weighted / total;
        weight_b = b_weighted / total;
    }
    size_t polar = SIZE_MAX;
    if (spec.space == NT_COLOR_HSL || spec.space == NT_COLOR_HSV) polar = 0;
    if (spec.space == NT_COLOR_OKLCH) polar = 2;
    for (size_t i = 0; i < (spec.space == NT_COLOR_CMYK ? 4u : 3u); ++i) {
        if (i == polar) {
            const bool missing_a = powerless(first) || (premultiplied && first.alpha == 0.0);
            const bool missing_b = powerless(second) || (premultiplied && second.alpha == 0.0);
            const double x = first.channel[i], y = second.channel[i];
            if (missing_a || missing_b)
                result.channel[i] = missing_a ? (missing_b ? 0.0 : y) : x;
            else
                result.channel[i] = nt_wrap_positive(x + progress * hue_delta(y - x, spec.hue), 360.0);
        } else {
            const double value = weighted(first.channel[i], second.channel[i], weight_a, weight_b);
            if (!isfinite(value)) return NT_RESULT_OUT_OF_RANGE;
            result.channel[i] = (spec.space == NT_COLOR_OKLAB || spec.space == NT_COLOR_OKLCH) &&
                                i != 0 ? value : unit_clip(value);
        }
    }
    *out = result;
    return NT_RESULT_OK;
}

NtResult nt_palette_lookup(NtPaletteView palette, size_t index, NtRgba *out)
{
    if (!out || !palette.colors || palette.count == 0) return NT_RESULT_INVALID_ARGUMENT;
    if (palette.count > SIZE_MAX / sizeof(*palette.colors) || index >= palette.count)
        return NT_RESULT_OUT_OF_RANGE;
    *out = palette.colors[index];
    return NT_RESULT_OK;
}

NtResult nt_palette_nearest(NtPaletteView palette, NtColor source, NtGamutPolicy gamut,
                            size_t *out_index)
{
    if (!out_index || !palette.colors || palette.count == 0) return NT_RESULT_INVALID_ARGUMENT;
    if (palette.count > SIZE_MAX / sizeof(*palette.colors)) return NT_RESULT_OUT_OF_RANGE;
    NtColor linear;
    const NtResult status = nt_color_convert(source, NT_COLOR_LINEAR_SRGB, gamut, &linear);
    if (status != NT_RESULT_OK) return status;
    /* A common scale leaves the distance ordering unchanged and avoids
     * rounding opposite byte alphas unevenly around an exact midpoint. */
    const double alpha = linear.alpha * 255.0;
    const double r = linear.channel[0] * alpha, g = linear.channel[1] * alpha;
    const double b = linear.channel[2] * alpha;
    size_t best = 0;
    double best_distance = INFINITY;
    for (size_t i = 0; i < palette.count; ++i) {
        const NtRgba entry = palette.colors[i];
        const NtColor c = nt_color_from_rgba(entry);
        const double entry_alpha = entry.alpha;
        const double dr = nt_srgb_decode(c.channel[0]) * entry_alpha - r;
        const double dg = nt_srgb_decode(c.channel[1]) * entry_alpha - g;
        const double db = nt_srgb_decode(c.channel[2]) * entry_alpha - b;
        const double da = entry_alpha - alpha;
        const double distance = dr*dr + dg*dg + db*db + da*da;
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    *out_index = best;
    return NT_RESULT_OK;
}
