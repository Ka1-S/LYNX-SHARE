/* LYNX addition (round 190nw): see lynx_xmp_extra.c. */
#ifndef LYNX_XMP_EXTRA_H
#define LYNX_XMP_EXTRA_H

#include "../include/libxmp-lite/xmp.h"

#ifdef __cplusplus
extern "C" {
#endif

double lynx_xmp_sample_rate(xmp_context ctx, int smp);
double lynx_xmp_frame_ms(xmp_context ctx);
int lynx_xmp_period_type(xmp_context ctx);
int lynx_xmp_sustain_loop(xmp_context ctx, int smp, int *start, int *end);

#ifdef __cplusplus
}
#endif

#endif
