#ifndef NOCTURNE_INTERNAL_TIMELINE_H
#define NOCTURNE_INTERNAL_TIMELINE_H

#include <nocturne/timeline.h>

/* Creation validates a full mapping before the retained timeline uses it. */
NtResult nt_track_time_validated(const NtTrackTime *timing, NtTimeUs elapsed, NtTimeUs *out);

#endif
