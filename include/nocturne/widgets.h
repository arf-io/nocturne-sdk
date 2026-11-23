#ifndef NOCTURNE_WIDGETS_H
#define NOCTURNE_WIDGETS_H

#include <nocturne/layout.h>
#include <nocturne/text_layout.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NtLabelStyle {
    NtWidthPolicy width;    /* Set both ambiguous and emoji widths. */
    NtWrapMode wrap;
    NtTextAlign align;
    NtDirection direction;
    uint32_t tab_stop;      /* Must be positive. */
    bool ellipsis;
    NtCellStyle color;
} NtLabelStyle;

typedef struct NtLabelOptions { NtByteView text; NtLabelStyle style; } NtLabelOptions;

typedef struct NtPanelOptions {
    NtRgba fill;
    NtBoxInsets padding;
    bool framed, ascii;
    NtCellStyle border;
    NtLabelOptions title;
    const NtClipMask *body_mask;  /* Optional child-local shape. */
} NtPanelOptions;

typedef struct NtRuleOptions {
    bool vertical, ascii;
    NtCellStyle color;
} NtRuleOptions;

/* Stock components own copies of counted text and optional mask spans.
 * A label and a rule are leaves. A panel or fill-only box has at most one
 * direct child, which can itself be a container. Setters retain the mount
 * and either replace all options or leave the prior options intact. */
NtResult nt_label_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                        const NtLabelOptions *label, NtNodeId *out);
NtResult nt_label_set(NtTree *tree, NtNodeId node, const NtLabelOptions *label);
NtResult nt_panel_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                        const NtPanelOptions *panel, NtNodeId *out);
NtResult nt_panel_set(NtTree *tree, NtNodeId node, const NtPanelOptions *panel);
NtResult nt_rule_mount(NtTree *tree, NtNodeId parent, NtMountOptions options,
                       NtRuleOptions rule, NtNodeId *out);
NtResult nt_rule_set(NtTree *tree, NtNodeId node, NtRuleOptions rule);

#ifdef __cplusplus
}
#endif
#endif
