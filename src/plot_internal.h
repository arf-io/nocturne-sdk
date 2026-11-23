#ifndef NOCTURNE_INTERNAL_PLOT_H
#define NOCTURNE_INTERNAL_PLOT_H

#include <nocturne/plot.h>

bool nt_plot_domain_valid(NtPlotDomain domain);
bool nt_plot_aspect_valid(double aspect);
/* Fit continuous extents with an authored ratio of physical width to height. */
NtPoint nt_plot_fit(NtPoint extents, double aspect, double width, double height);
NtPoint nt_plot_project_inside(const NtPlotViewport *viewport, NtPoint value);

#endif
