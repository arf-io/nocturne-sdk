# Text effect recipes

Use the same layout for measuring, caret mapping and effect painting. For
each frame, set `NtTextPaintOptions.elapsed_us` from your playback clock,
choose full or reduced motion, and draw onto the current scene. Use
`nt_paint_text_effect` in component paint callbacks.

This reveal gives each visible grapheme a 75 ms fade, with starts 24 ms
apart in logical reading order. While the fade runs, the accent glyphs
rotate every 15 ms. A grapheme with no matching-width accent keeps its
authored glyph.

```c
#include <nocturne/text_effect.h>

NtResult paint_title(const NtSurfaceView *surface, const NtTextLayout *layout,
                     NtTimeUs elapsed, NtMotionPolicy motion)
{
    static const NtByteView alphabet[] = {{"*", 1}, {"+", 1}, {".", 1}};
    const NtTextEffect effect = {
        .duration_us = 75000,
        .schedule = {.stagger_us = 24000},
        .opacity_easing = NT_EASE_OUT,
        .glyphs = {NT_TEXT_FLIPBOARD, alphabet, 3, 15000, 0},
        .seed = 42
    };
    const NtTextPaintOptions paint = {
        .base = {.color = {.foreground = {218, 221, 224, 255}}},
        .elapsed_us = elapsed,
        .motion = motion
    };
    return nt_text_effect_paint(surface, (NtCellPoint){2, 1}, layout, &paint, &effect);
}
```

Set `visibility` to `NT_TEXT_HIDE` to run an exit. Schedule its start
from the caller's clock. For the formulas below, `p` is the
sample's `progress`, `d` is `travel`, and `x` is the original
leading cell plus half the cluster width. Compute row midpoint `m`
from `line_begin` and `line_end`. Use `a = 1 - p` for fading out and
`a = p` for fading in. A stable seeded phase comes from sampling
`NtRandomTrack` at zero with the cluster's seed and identity, or by
setting `NtTextMotion.seeded_phase`. A trail example uses
`.trails = {2, 16000, .5}`.

The table covers the 36 entries in the [audited text recipes](recipes/text.tsv).
The reference IDs and selectors identify the behavior being mapped.
Durations, curves, distances, alphabets and trails are application
parameters. In a transform, add the listed offsets to the pose and
keep opacity in the interval from zero through one.

| Catalogue reference | Algorithm / profile | Public recipe |
|---|---:|---|
| `text/direct-ink` | 0 / 0 | Set duration and stagger to zero. The target appears at onset. |
| `text/ink-dissolve` | 1 / 0 | Set a positive duration and select an opacity easing such as `NT_SMOOTH`. |
| `text/glyph-rain` | 2 / 0 | Add seeded lateral offsets with `nt_text_pose_noise`. Add horizontal wind and negative vertical travel through `nt_text_pose_motion` with eased opacity and optional trails. |
| `text/cipher-resolve` | 3 / 0 | Set `NT_TEXT_SCRAMBLE` with a seeded, interval-stepped alphabet and eased opacity. |
| `text/station-board` | 4 / 0 | Set `NT_TEXT_FLIPBOARD` with positive rotations. In the transform multiply opacity by a periodic envelope from `local_us % interval_us`. |
| `text/ribbon-left` | 5 / 0 | Set zero span and positive `stagger_us` with logical order. |
| `text/ribbon-right` | 6 / 0 | Set zero span and positive `stagger_us` with reverse logical order. |
| `text/travelling-line` | 7 / 0 | Pass the viewport width plus margin to the transform. Use a shared horizontal travel of that distance and an independent opacity easing. |
| `text/satellite-type` | 8 / 0 | Set elliptical `orbit`, `cycles` and `seeded_phase` on `NtTextMotion`. Use eased travel with separate opacity and optional trails. |
| `text/star-map-type` | 9 / 0 | Set a seeded phase, nonzero orbit radii and zero cycles. The initial angle persists as travel tapers. Add eased opacity and optional trails. |
| `text/prism-stitch` | 10 / 0 | Apply horizontal travel to even logical indices and vertical travel to odd ones. Set a hue rotation proportional to `d`. |
| `text/gravity-loom` | 11 / 0 | Combine a seeded ellipse with wind and vertical translation in `NtTextMotion`. Use `NT_EASE_IN_OUT` for displacement. |
| `text/scanline-type` | 12 / 0 | Use a short scramble duration, a short glyph interval, `NT_EASE_OUT` opacity and lateral offset of `±d` by logical index parity. |
| `text/elastic-type` | 13 / 0 | Add vertical offset `height * d * d * cos(4π * (1 - d))` for damped overshoot toward the settled row. |
| `text/keystep` | 14 / 0 | Use zero-duration slots with explicit `delay_us` and a positive stagger. Choose reveal or hide and the desired cluster order. |
| `text/curtain-type` | 15 / 0 | Use `-distance * d` for clusters with `x < m` and `distance * d` for the others. Apply opacity easing independently. |
| `text/glyph-fall` | 16 / 0 | Use hide with vertical offset `height * p * p` and smooth seeded lateral noise. |
| `text/ash-type` | 17 / 0 | Use hide with an upward lift followed by falling motion, for example `height * (2*p*p - p)`, plus seeded lateral drift. After a travel threshold choose an ash glyph by logical index from a caller-owned, width-matched alphabet. |
| `text/instant-ink` | 0 / 1 | Use a zero-span reveal or hide and the target's regular style. |
| `text/velvet-dissolve` | 1 / 1 | Use a linear effect clock and replace pose opacity with `nt_ease(NT_EASE_IN_OUT, a, &pose->opacity)` in the transform. |
| `text/skyfall-type` | 2 / 1 | Combine seeded lateral noise and wind with positive vertical travel for hide and negative for reveal. Use linear opacity and optional trails. |
| `text/static-resolve` | 3 / 1 | Use `NT_TEXT_SCRAMBLE` and linear reveal or hide opacity. Delay the start to align the first substitutions with the authored onset. |
| `text/departure-board` | 4 / 1 | Use `NT_TEXT_FLIPBOARD` with zero rotations to advance by interval. Add a small vertical sine wave weighted by `d`. |
| `text/weft-left` | 5 / 1 | Set negative horizontal `NtTextMotion.travel.x` and use separate eased opacity. |
| `text/weft-right` | 6 / 1 | Set positive horizontal `NtTextMotion.travel.x` and use separate eased opacity. |
| `text/rising-ink` | 18 / 1 | Use negative vertical travel for an upward exit, or positive initial vertical offset for a reveal that rises into place. Ease opacity separately. |
| `text/falling-ink` | 16 / 1 | Use positive vertical travel for a downward exit, or negative initial vertical offset for a reveal that falls into place. Ease opacity separately. |
| `text/ticker-type` | 7 / 1 | Use horizontal travel based on `line_end - line_begin` plus margin. Add seeded lateral scatter and choose opacity separately. |
| `text/iris-type` | 19 / 1 | Add horizontal offset `(x - m) * expansion * d` with a shared expansion factor. |
| `text/constellation-type` | 9 / 1 | Add seeded orbit and hue with linear opacity and optional trails. For a phase driven by `d`, set `phase_turns = cycles * d` in the transform and set the helper's `cycles` to zero. |
| `text/prism-weave` | 20 / 1 | Set rank phase with `rank_turns`, alternate vertical travel by parity, and enable travel-weighted hue and trails. |
| `text/ink-tide` | 21 / 1 | Add vertical offset `amplitude * d * sin(2π * (cycles*p - frequency*x))` and horizontal wind proportional to `d`. |
| `text/comet-stitch` | 22 / 1 | Use lateral travel, a seeded vertical ellipse, hue and trails. Zero the horizontal orbit radius to keep lateral travel independent of the arc. |
| `text/orbital-type` | 8 / 1 | Set elliptical radii and common `phase_turns = cycles * d` with zero rank phase and zero helper cycles. Use optional trails. |
| `text/folded-type` | 23 / 1 | Add alternating lateral lanes `±distance * d` and vertical fold `height * sin(π*d)`. |
| `text/zipper-type` | 24 / 1 | Add horizontal travel for even clusters and vertical travel for odd clusters, each weighted by `d`. |

The [animated text contract](CONTRACTS.md#animated-text-effects) defines
sampling, grapheme width, clipping and settlement. The [installed text effect
consumer](../tests/consumer/text_effect.c) exercises recipes in both mounted and
surface paint paths.
