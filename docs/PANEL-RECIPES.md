# Panel transition recipes

Paint panel content into a caller-owned frame. Project a crop of that
frame into a fixed viewport. A pose handles anchored scale, rotation,
translation and opacity. A remap can change where each destination
cell samples the crop or how much ink it contributes. The same viewport
can clip pieces of a panel while the pieces move independently.

This example opens a centered aperture with a small horizontal slide.
It uses a circular remap through the full viewport. On reduced motion
it draws the settled panel through the rectangular viewport.

```c
#include <nocturne/panel_transform.h>
#include <math.h>

static NtResult iris(void *context, const NtPanelSample *sample, NtPanelPixel *pixel)
{
    const double reveal = *(const double *)context;
    const double cx = sample->viewport_size.columns * .5;
    const double cy = sample->viewport_size.rows * .5;
    const double x = sample->destination.x - cx;
    const double y = sample->destination.y - cy;
    pixel->opacity = hypot(x, y) <= reveal * hypot(cx, cy) ? 1 : 0;
    return NT_RESULT_OK;
}

NtResult paint_panel_reveal(const NtSurfaceView *surface, const NtFrame *content,
                            NtCellRect viewport, NtTimeUs elapsed, NtMotionPolicy motion)
{
    const NtPanelPose rest = {.scale = {1, 1}, .opacity = 1};
    const NtPanelTransition slide = {
        .from = {.offset = {-4, 0}, .scale = {1, 1}, .opacity = 0},
        .to = rest, .duration_us = 200000, .easing = NT_EASE_OUT
    };
    NtPanelPose pose;
    const NtResult result = nt_panel_pose_at(&slide, elapsed, motion, &pose);
    if (result != NT_RESULT_OK) return result;
    double reveal = pose.opacity;
    const NtPanelLayer layer = {
        .source = content, .region = {{0, 0}, nt_frame_size(content)},
        .viewport = viewport, .pose = pose, .elapsed_us = elapsed,
        .remap = motion == NT_MOTION_FULL ? iris : NULL, .remap_context = &reveal
    };
    return nt_surface_project_panels(surface, &layer, 1);
}
```

In these recipes, `v` is a visible fraction in zero through one and
`d = 1 - v`. For an entrance, start at zero and bring `v` to one.
For an exit, run it in the opposite direction. Use the same geometry
and opacity functions for both. Functions of row or column use the
normalized coordinate within the moving piece, measured from zero to
one, unless described as centered. Approximate local depth with a
piece's scale, position and opacity. Composite rear pieces first.

When using a remap, evaluate inverse geometry. To narrow about anchor
`a` by factor `s`, sample `a + (x - a)/s`; outside the original crop
the layer contributes no ink. An exactly collapsed piece can use a
zero scale. For row or band formulas, derive the band from the
destination coordinate or use one cropped layer per piece. Separate
the panel pose from its fixed viewport so offscreen travel draws through
the existing clip. For a seeded treatment, sample `NtRandomTrack` at
zero with each fragment's stable identity as its stream.

The table maps the 43 catalogue IDs to the [audited panel recipes](recipes/panel.tsv).
Use the same public operations with your own sizes, timing and depth.

| Catalogue reference | Algorithm / profile | Public recipe |
|---|---:|---|
| `window-transition/glass-dissolve` | 0 / 0 | Use the rest geometry and ease `pose.opacity` with `v`. |
| `window-transition/elastic-pop` | 1 / 0 | Scale both axes about the center. Use scale `1 - d²*cos(3π*v)` for damped overshoot and opacity `v`. |
| `window-transition/lamp-genie` | 2 / 0 | In the remap, divide each row's horizontal distance from the caller's anchor by `v + d*r³`, with row factor `r` increasing away from the pinched edge. Curve the row's lateral offset with `d*sin(π*r)`. |
| `window-transition/scroll-up` | 3 / 0 | Scale vertically by `v` about the top. Use inverse row shear or stripe offsets proportional to `d*sin(π*r)` for the rolled profile and ease opacity with `v`. |
| `window-transition/scroll-down` | 4 / 0 | Scale vertically about the bottom. Mirror the row shear of the upward roll and fade with `v`. |
| `window-transition/rail-left` | 5 / 0 | Set horizontal scale to `v` about the left edge and sample opacity from `v`. |
| `window-transition/rail-right` | 6 / 0 | Set horizontal scale to `v` about anchor `(width, 0)` and sample opacity from `v`. |
| `window-transition/rail-top` | 7 / 0 | Set vertical scale to `v` about the top edge and sample opacity from `v`. |
| `window-transition/rail-bottom` | 8 / 0 | Set vertical scale to `v` about anchor `(0, height)` and sample opacity from `v`. |
| `window-transition/center-aperture` | 9 / 0 | Scale both axes by `v` about `(width/2, height/2)`. |
| `window-transition/northwest-aperture` | 10 / 0 | Scale both axes by `v` about `(0, 0)`. |
| `window-transition/northeast-aperture` | 11 / 0 | Scale both axes by `v` about `(width, 0)`. |
| `window-transition/southwest-aperture` | 12 / 0 | Scale both axes by `v` about `(0, height)`. |
| `window-transition/southeast-aperture` | 13 / 0 | Scale both axes by `v` about `(width, height)`. |
| `window-transition/circular-iris` | 14 / 0 | In the remap compare center distance with a radius sampled from its own track. Sample layer opacity from a separate track to fade the revealed portion. |
| `window-transition/shard-burst` | 15 / 0 | Crop into normalized fragments. Sample a stable angle and depth per piece, offset by `d*distance*(cos(angle), sin(angle))` and composite in depth order. Use opacity `v²`. |
| `window-transition/concertina` | 16 / 0 | Crop into a chosen number of horizontal bands. Compress vertically by `v`, pivot alternating bands about opposite edges and give alternating bands a smaller scale or opacity for depth. |
| `window-transition/portal-fold` | 17 / 0 | Anchor at the center, scale uniformly, rotate by an authored number of turns times `d`, and use a second scale or offset for the depth cue. |
| `window-transition/orbital-bloom` | 18 / 0 | Use the centered turn and scale. Add offset `(travel*d, -lift*sin(π*d))`, taper the depth scale and ease opacity. |
| `window-transition/venetian` | 19 / 0 | Crop repeated stripes and assign each stripe its own pivot, signed turn and depth scale. Give farther stripes earlier layer order. |
| `window-transition/ribbon-unfurl` | 20 / 0 | Divide into bands. Set roll scale and signed turn from band position and `d`; add sinusoidal lateral offset proportional to `d`. |
| `window-transition/silk-dissolve` | 0 / 1 | Use the rest geometry and author a softer opacity curve with `NT_SMOOTHER`. |
| `window-transition/center-bloom` | 9 / 1 | Use scale `(v,v)`, opacity ramp, and a caller-selected anchor such as the center or top-right corner. |
| `window-transition/northwest-bloom` | 10 / 1 | Scale about the top-left corner. Ease the progress used for scale. |
| `window-transition/northeast-bloom` | 11 / 1 | Scale about the top-right corner. Ease the progress used for scale. |
| `window-transition/southwest-bloom` | 12 / 1 | Scale about the bottom-left corner. Ease the progress used for scale. |
| `window-transition/southeast-bloom` | 13 / 1 | Scale about the bottom-right corner. Ease the progress used for scale. |
| `window-transition/circle-bloom` | 14 / 1 | Increase the remap's accepted radius from zero through the viewport corner distance. Set opacity to one at each admitted cell. |
| `window-transition/reel-up` | 3 / 1 | Compress vertically around the top, fade with `v`, and for the first part of the reveal mask cells beyond a moving top-relative row limit. |
| `window-transition/reel-down` | 4 / 1 | Compress vertically around the bottom and fade with `v`. At onset apply a mirrored bottom-relative row limit. |
| `window-transition/pocket-left` | 5 / 1 | Set offset `(-width*d, 0)` with identity scale in the original viewport. |
| `window-transition/pocket-right` | 6 / 1 | Set offset `(width*d, 0)` in the original viewport. |
| `window-transition/pocket-top` | 7 / 1 | Set offset `(0, -height*d)` in the original viewport. |
| `window-transition/pocket-bottom` | 8 / 1 | Set offset `(0, height*d)` in the original viewport. |
| `window-transition/spring-pop` | 1 / 1 | Scale around the bottom center using `v + k*max(0, v-.65)*d` with `k` large enough for one late overshoot. For example, `k = 5` yields scale above one near `v = .9`. Fade through opacity `v`. |
| `window-transition/ribbon-genie` | 2 / 1 | Narrow rows toward an anchor using `v + d*r^q` for chosen power `q > 1`. Bend their sampling offset toward the base with `d*(1-r)²` and fade with `v`. |
| `window-transition/fragment-scatter` | 15 / 1 | Use crops per cell, keeping a width-two footprint in one crop. Add independent seeded x and y offsets weighted by `d`, plus shared vertical lift. Fade each layer through `v`. |
| `window-transition/accordion-fold` | 16 / 1 | Compress along x in the pose. In a remap use vertical shear proportional to `d*sin(2π*folds*c)`, where `c` is normalized column. Fade independently. |
| `window-transition/origami` | 21 / 1 | Split at the center axis and pinch both pieces toward it. Apply opposite turns and opposite vertical shear to the halves. |
| `window-transition/vortex-fold` | 22 / 1 | Scale about the center by `v²` and rotate by a configurable number of turns times `d`. |
| `window-transition/ribbon-unspool` | 23 / 1 | Give bands ranked delays from `nt_track_schedule`. Apply local slide and a sinusoidal arch within each band, with local opacity. |
| `window-transition/petal-bloom` | 24 / 1 | In centered polar coordinates use boundary `radius = v*extent*(1 + lobes_amplitude*cos(lobes*angle))`. Mask beyond it and sample radius divided by the same local scale within it. |
| `window-transition/gravity-well` | 25 / 1 | Use the affine pose for turns. At normalized source radius `r`, project radial distance as `v*r*(1-d*r/2)`. In the remap solve for source radius on the monotone normalized interval and use opacity `v`. |

For a striped iris combining an affine slide, its own remap,
independent background samples and coverage, see the
[installed C consumer](../tests/consumer/panel_transform.c).
