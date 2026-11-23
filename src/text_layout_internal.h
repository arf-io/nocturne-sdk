#ifndef NOCTURNE_INTERNAL_TEXT_LAYOUT_H
#define NOCTURNE_INTERNAL_TEXT_LAYOUT_H

#include <nocturne/text_layout.h>

bool nt_text_layout_options_valid(const NtTextLayoutOptions *options);
NtWidthPolicy nt_text_layout_width_policy(const NtTextLayout *layout);

#endif
