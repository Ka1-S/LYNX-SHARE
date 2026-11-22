/* LYNX addition (round 190nw, not part of libxmp-lite 4.7.4; MIT like the library): read-only facts the module import
 * needs that libxmp keeps in its internal context and does not publish through xmp.h. */

#include "common.h"
#include "player.h"
#include "mixer.h"
#include "period.h"
#include "virtual.h"
#include "lynx_xmp_extra.h"

#include <math.h>

double libxmp_get_frame_time(struct context_data *ctx);

/* The rate (Hz) sample `smp` plays at on libxmp's note 60 (period 428, its "C-5"): the sample's c5spd. */
double lynx_xmp_sample_rate(xmp_context opaque, int smp)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct module_data *m = &ctx->m;
	if (smp < 0 || smp >= m->mod.smp || m->xtra == NULL)
		return (double)m->c4rate;
	return m->xtra[smp].c5spd > 0.0 ? m->xtra[smp].c5spd : (double)m->c4rate;
}

/* The length of the frame (tick) just played, in milliseconds, without the rounding of xmp_frame_info.frame_time. */
double lynx_xmp_frame_ms(xmp_context opaque)
{
	return libxmp_get_frame_time((struct context_data *)opaque);
}

/* 0 Amiga periods, 1 Amiga periods with the range limit, 2 linear, 3 C-speed (libxmp's PERIOD_*). */
int lynx_xmp_period_type(xmp_context opaque)
{
	return ((struct context_data *)opaque)->m.period_type;
}

/* The sustain loop of sample `smp` (IT): 1 and its frames [start, end) when it has one, else 0. */
int lynx_xmp_sustain_loop(xmp_context opaque, int smp, int *start, int *end)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct module_data *m = &ctx->m;
	if (smp < 0 || smp >= m->mod.smp || m->xtra == NULL || !(m->mod.xxs[smp].flg & XMP_SAMPLE_SLOOP))
		return 0;
	*start = m->xtra[smp].sus;
	*end = m->xtra[smp].sue;
	return *end > *start;
}

/* Round 190ob: 1 when the sample of module channel `chn` has played to its end (libxmp then reports its volume as 0:
 * the note is silent because its sample ended, not because its volume changed), else 0. */
int lynx_xmp_sample_ended(xmp_context opaque, int chn)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;
	if (p->xc_data == NULL || chn < 0 || chn >= ctx->m.mod.chn)
		return 0;
	return (p->xc_data[chn].note_flags & NOTE_SAMPLE_END) != 0;
}

/* Round 190om: the pitch module channel `chn` sounds at now, from its mixer voice's period (what the mixer plays: every
 * slide, vibrato, arpeggio, envelope and limit applied), in libxmp's note scale: 60 = the sample's c5spd rate, one unit a
 * semitone, fractional. Also the sample the voice plays and its position (frames). 1 when a voice plays, else 0. */
int lynx_xmp_channel_pitch(xmp_context opaque, int chn, double *pitch, int *smp, double *pos)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;
	struct mixer_voice *vi;
	int voc;
	if (p->virt.voice_array == NULL || chn < 0 || chn >= ctx->m.mod.chn)
		return 0;
	voc = libxmp_virt_mapchannel(ctx, chn);
	if (voc < 0)
		return 0;
	vi = &p->virt.voice_array[voc];
	if (vi->period < 0.1 || vi->smp < 0)
		return 0;
	*pitch = 12.0 * log(PERIOD_BASE / vi->period) / log(2.0);
	*smp = vi->smp;
	*pos = vi->pos;
	return 1;
}

/* Round 190om: which tracker's rules the player follows (libxmp's READ_EVENT_*: 0 MOD, 1 FT2, 2 ST3, 3 IT, ...). */
int lynx_xmp_player_mode(xmp_context opaque)
{
	return ((struct context_data *)opaque)->m.read_event_type;
}
