#!/usr/bin/env python3
"""taillab.py — reverb / modulation measurement lab and the TAIL SCORE (round 190bp).

The waveform score (scripts/fxparity.py score(), untouched) needs a waveform null, which a recirculating or
modulated effect cannot give: two correct renders of the same reverb do not null against each other. This lab
measures what a tail *is* — decay per octave band, early decay, echo-density build-up, spectral balance over time,
stereo correlation over time, time variance (modulation) — and scores two renders on those, blind to phase and to
the modulators' phase. Both scores are always reported side by side; the tail score never replaces the old one.

Black box only: the inputs are WAV renders (tools/fxrender for LYNX, recorded host renders for references).

  taillab.py pair <ref.wav> <lynx.wav> [--stim <stimulus.wav>] [--kind verb|delay|mod] [--dry <gL>,<gR>,<latency>]
        one pair: the measurements, the sub-scores and the tail score of that file (JSON)
  taillab.py dir <ref-dir> <lynx-dir> --stim <stim-dir> [--kind verb|delay|mod] [--json out.json]
        every <setting>_<signal>.wav present in both: wave score (fxparity) and tail score per file, per setting
        and for the row. A setting's dry gain is read off its impulse render and removed from all its files.
  taillab.py stats <render.wav> [--stim <stimulus.wav>]     measurements of one impulse render (JSON)
  taillab.py pilot <render.wav> <hz> [t0 t1]               modulation rate / depth seen on a steady pilot tone
  taillab.py probes <out-dir> [rate]                        writes the lab's stimuli (impulses at three levels each
                                                            followed by a noise burst, pilot tones, pink noise)
  taillab.py engine <fxrender> <effect> <probe-dir> <out-dir> [fxrender options...]
        renders the probes through one LYNX effect and prints its decay / density / stereo / modulation table
  taillab.py selftest                                       unit tests of the metric code (synthetic signals)

Impulse stimulus layout (tp172b/c/d `impulse.wav` and `probes`): one sample impulse, silence, then a noise burst.
The stretch after the impulse is the impulse response; the burst is used for the time-variance index: a linear
time-invariant effect answers the burst with (impulse response * burst), any modulation makes it differ.

Tail score, per setting: mean of the impulse half and the programme half. Every part is 10 at a match and falls
linearly to 0 at the distance `FULL[part]`; a half is 10 minus the quadratic mean of what its parts miss (agg):
  impulse half
    decay    |log2 ratio| of T20 and T30 per octave band (late-slope RT where the range is too short)
    early    EDT per octave band (|log2|), clarity C80 (dB) and pre-delay (ms)
    relief   energy decay relief: |dB| between the per-band Schroeder curves on a 20 ms grid, level removed
    density  echo density (Abel & Huang NED): time to 0.5 and 0.9, and the late value
    balance  octave-band spectrum of four time slices of the response (shape only)
    stereo   L/R correlation per octave band in the same time slices
    mod      time-variance index per octave band (burst answer vs. the answer predicted from the impulse response)
    level    energy of the wet response
    dry      (only where a dry path is present) its gain; its latency is taken as the effect's time zero
  programme half (drums, music, ...: the wet part of each render)
    kind verb (diffuse tails):
      spec   long-term third-octave spectrum of the wet part, level removed
      env    level over time of the wet part in three wide bands (below 300 Hz, to 3 kHz, above), 160 ms
      level  mean third-octave level difference
      stereo L/R correlation per octave band 500 Hz - 8 kHz
    kind delay / mod (deterministic effects; mod compares the whole output, the others the wet part):
      spec   third-octave x 20 ms energy map, level removed (frames smoothed until each cell holds enough samples)
      level  energy
      stereo L/R correlation per octave band x 200 ms (delay) / per octave band over the file (mod: a flanger's
             correlation moves with its LFO phase)
      motion (kind mod only) modulation spectrum 0.25-16 Hz of the band transfer and of the L/R level difference
Every distance of the diffuse-tail parts first loses its dead zone (DZ): the amount two phase-realisations of one
reverb differ by on that estimator, so that a dense reverb scores >= 9.5 against its own second render (measured:
LYNX against itself at another modulator phase 9.6 ... 10; sparse cloud modes, whose response is a handful of
echoes rather than a diffuse field, repeat only to 7.7 ... 9.1 - on the reference as well).
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fxparity  # noqa: E402

OCT = (63, 125, 250, 500, 1000, 2000, 4000, 8000)
THIRDS = tuple(50.0 * 2 ** (k / 3.0) for k in range(26))          # 50 Hz ... 16 kHz
SLICES = ((0.0, 0.08), (0.08, 0.3), (0.3, 1.0), (1.0, 3.0))       # seconds after the onset
GRID = 0.02                                                       # s, relief / programme map frame
# distance at which a part scores 0 (see the module text). Changing one changes every tail score: bump VERSION.
FULL = {'decay': 0.5, 'edt': 1.0, 'c80': 6.0, 'predelay': 30.0, 'relief': 6.0, 'ned_t': 2.0, 'ned_late': 0.5,
        'balance': 6.0, 'stereo': 0.5, 'mod': 0.5, 'level': 6.0, 'spec': 6.0, 'motion': 10.0}
# Dead zones: what two phase-realisations of one and the same reverb differ by on these estimators (sampling noise of
# a band of width B over a time T goes with 1/sqrt(B T); the programme constants are the largest distance between two
# recordings of one reference state on the banks' drums / music, see the anchors in docs/PARITY.md). A distance inside its dead zone is no evidence.
DZ = {'t20': 1.0, 't30': 0.6, 'edt': 2.0, 'balance': 4.0, 'corr': 1.13, 'relief': 3.5, 'level': 0.2, 'c80': 0.2,
      'p_spec': 0.4, 'p_env': 0.65, 'p_level': 0.15, 'p_stereo': 0.03}
P_FULL = 4.0     # dB of long-term spectrum / envelope difference that costs all points (kind verb)
VERSION = 1
BW = tuple(c * (2 ** 0.5 - 2 ** -0.5) for c in OCT)               # octave bandwidths, Hz
NED_GAUSS = 0.3173105078629141                                    # erfc(1 / sqrt(2))


def lin(d, full):
    return float(max(0.0, 10.0 * (1.0 - d / full)))


def db(x):
    return 10.0 * np.log10(np.maximum(x, 1e-30))


def agg(parts):
    """A half's score from its parts: 10 minus the quadratic mean of what each part misses. One part that is plainly
    wrong (a decay time off by a factor) therefore weighs more than the same points spread thinly over all parts."""
    v = np.array(list(parts.values()), float)
    return round(float(10.0 - np.sqrt(np.mean((10.0 - v) ** 2))), 3) if len(v) else None


# ------------------------------------------------------------------------------------------------ filters
def band_signals(x, sr, centers=OCT):
    """Zero-phase octave bands of a mono signal (FFT masks with raised edges a third of an octave wide that sum
    to one between neighbours). Returns [len(centers), len(x)]."""
    n = 1 << int(np.ceil(np.log2(len(x) + sr // 4)))
    X = np.fft.rfft(x, n)
    lf = np.log2(np.maximum(np.fft.rfftfreq(n, 1.0 / sr), 1e-9))
    out = np.empty((len(centers), len(x)))
    for k, c in enumerate(centers):
        lo, hi = c / 2 ** 0.5, min(c * 2 ** 0.5, sr * 0.47)
        w = np.clip((lf - np.log2(lo)) * 3 + 0.5, 0, 1) * np.clip((np.log2(hi) - lf) * 3 + 0.5, 0, 1)
        out[k] = np.fft.irfft(X * w, n)[:len(x)]
    return out


def frames(p, hop):
    n = len(p) // hop
    return p[:n * hop].reshape(n, hop).sum(1)


# ------------------------------------------------------------------------------------------------ decay
def decay_curve(p, sr, hop_s=0.005):
    """Schroeder decay of an energy signal p (per sample, starting at the onset). The integration stops where the
    response meets its floor (or the end of the capture) and the missing rest is added as the integral of the
    late exponential, so a capture shorter than the tail does not bend the curve. Returns (edc dB per 5 ms frame,
    late-slope RT60 or None, usable frames)."""
    hop = max(1, int(hop_s * sr))
    e = frames(p, hop)
    if len(e) < 12 or e.max() <= 0:
        return np.zeros(1), None, 0
    sm = np.convolve(e, np.ones(5) / 5.0, 'same')
    pk = int(np.argmax(sm))
    noise = float(np.mean(e[-max(4, len(e) // 20):]))
    above = np.flatnonzero(sm > noise * 4.0)
    end = int(above[-1]) + 1 if len(above) else len(e)
    low = np.flatnonzero(sm[pk:] < sm[pk] * 1e-6)                  # below -60 dB the band filters' own skirts take over
    if len(low):
        end = min(end, pk + int(low[0]))
    end = max(end, min(len(e), pk + 10))
    rt_late, comp = None, 0.0
    a = pk + int((end - pk) * 0.4)
    if end - a >= 8:
        seg = np.log(np.maximum(sm[a:end], 1e-300))
        k, c = np.polyfit(np.arange(a, end), seg, 1)
        if k < -1e-9:
            rt_late = float(-np.log(1e6) / k * hop / sr)
            comp = float(np.exp(c + k * end) / (1.0 - np.exp(k)))
    tail = np.cumsum(e[:end][::-1])[::-1] + comp
    return db(tail / tail[0]), rt_late, end


def rt_from(edc, hop_s, hi, lo):
    """RT60 from the regression of a decay curve between hi and lo dB (None if the curve does not get there)."""
    i0, i1 = int(np.argmax(edc <= hi)), int(np.argmax(edc <= lo))
    if edc.min() > lo or i1 <= i0 + 3:
        return None
    k = np.polyfit(np.arange(i0, i1) * hop_s, edc[i0:i1], 1)[0]
    return float(-60.0 / k) if k < 0 else None


# ------------------------------------------------------------------------------------------------ echo density
def ned_curve(x, sr, win_s=0.025, hop_s=0.005):
    """Normalised echo density (Abel & Huang 2006): share of samples outside one standard deviation in a sliding
    Hann-weighted window, over the Gaussian share. ~0 for a few isolated echoes, ~1 for a dense (Gaussian) tail."""
    w, hop = int(win_s * sr), int(hop_s * sr)
    n = (len(x) - w) // hop + 1
    if n < 1:
        return np.zeros(0), np.zeros(0)
    idx = np.arange(w)[None, :] + hop * np.arange(n)[:, None]
    fr = x[idx]
    wt = np.hanning(w)
    wt /= wt.sum()
    var = (fr ** 2 * wt).sum(1)
    out = ((np.abs(fr) > np.sqrt(var)[:, None]) * wt).sum(1) / NED_GAUSS
    return out, var


def ned_stats(x, sr):
    """Time (s, window centre from the start of x) at which the NED first stays above 0.5 / 0.9, the late NED
    (0.15 - 0.6 s) and the curve."""
    ned, var = ned_curve(x, sr)
    if len(ned) < 8:
        return {'t50': None, 't90': None, 'late': None}
    sm = np.convolve(np.pad(ned, 1, 'edge'), np.ones(3) / 3.0, 'valid')
    live = var > var.max() * 1e-7
    t = (np.arange(len(ned)) * 0.005) + 0.0125

    def first(th):
        ok = (sm >= th) & live
        return float(t[int(np.argmax(ok))]) if ok.any() else None
    late = (t >= 0.15) & (t <= 0.6) & live
    return {'t50': first(0.5), 't90': first(0.9), 'late': float(ned[late].mean()) if late.sum() >= 4 else None}


# ------------------------------------------------------------------------------------------------ probe layout
def find_probe(stim, sr):
    """Impulse index, its amplitude and the noise burst's sample range in an impulse stimulus (None if it is not one)."""
    idx = np.flatnonzero(np.abs(stim[:, 0]) > 1e-9)
    if len(idx) == 0 or (len(idx) > 1 and idx[1] - idx[0] < int(0.05 * sr)):
        return None
    later = idx[1:]
    burst = (int(later[0]), int(later[-1]) + 1) if len(later) > 100 else None
    return {'imp': int(idx[0]), 'amp': float(stim[idx[0], 0]), 'burst': burst}


def dry_path(out, stim, sr=48000, maxlag=256):
    """Dry path of an impulse render: (gain left, gain right, latency in samples). The render deconvolved by its
    stimulus (impulse + noise burst) is the response itself; a dry path is one isolated positive spike within the
    first `maxlag` samples (a plug-in latency), the same on both channels, at least 0.1 and twice anything else
    there. Its gain is then read exactly at the impulse sample. (0, 0, 0) if there is none."""
    probe = find_probe(stim, sr)
    n = min(len(out), len(stim))
    nf = 1 << int(np.ceil(np.log2(n + maxlag + 1)))
    m = np.zeros(maxlag + 1)
    for c in (0, 1):
        S = np.fft.rfft(stim[:n, c], nf)
        p = np.abs(S) ** 2
        m += np.abs(np.fft.irfft(np.fft.rfft(out[:n, c], nf) * np.conj(S) / (p + 1e-4 * p.max() + 1e-30), nf)[:maxlag + 1])
    k = int(np.argmax(m))
    rest = m.copy()
    rest[max(0, k - 3):k + 4] = 0.0
    if probe is None or probe['imp'] + k >= len(out) or m[k] < 2.0 * rest.max():
        return (0.0, 0.0, 0)
    i = probe['imp']
    g = [float(out[i + k, c] / stim[i, c]) if abs(stim[i, c]) > 1e-9 else 0.0 for c in (0, 1)]
    if min(g) < 0.1 or abs(g[0] - g[1]) > 0.03 * max(g):                 # positive, and the same on both sides
        return (0.0, 0.0, 0)
    return (g[0], g[1], k)


def wet_of(out, stim, g):
    """The render minus its dry path g = (gain left, gain right, latency)."""
    w = out.copy()
    lag = int(g[2]) if len(g) > 2 else 0
    n = min(len(out) - lag, len(stim))
    if n > 0:
        w[lag:lag + n, 0] -= g[0] * stim[:n, 0]
        w[lag:lag + n, 1] -= g[1] * stim[:n, 1]
    return w


# ------------------------------------------------------------------------------------------------ impulse stats
def impulse_stats(out, stim, sr, g=None):
    """Measurements of an impulse render (see the module text). `out`, `stim`: [n, 2]."""
    probe = find_probe(stim, sr)
    if probe is None:
        raise ValueError('stimulus is not an impulse probe')
    if g is None:
        g = dry_path(out, stim, sr)
    wet = wet_of(out, stim, g)
    lag = int(g[2]) if len(g) > 2 else 0                                # the effect's latency: its time zero
    i0 = probe['imp'] + lag
    i1 = probe['burst'][0] + lag if probe['burst'] else len(wet)
    h = wet[i0:min(i1, len(wet))]
    st = {'sr': sr, 'dry': [round(float(g[0]), 4), round(float(g[1]), 4)], 'latency': lag}
    p = (h ** 2).sum(1)
    tot = float(p.sum())
    st['energy_db'] = float(db(tot / (probe['amp'] ** 2)))
    if tot <= 1e-20:
        st['silent'] = True
        return st
    cum = np.cumsum(p)
    on = int(np.argmax(cum >= tot * 1e-3))
    st['predelay_ms'] = on / sr * 1000.0
    n80 = on + int(0.08 * sr)
    st['c80_db'] = float(db(cum[min(n80, len(cum) - 1)] / max(tot - cum[min(n80, len(cum) - 1)], 1e-30)))
    st['centre_ms'] = float((p[on:] * np.arange(len(p) - on)).sum() / max(p[on:].sum(), 1e-30) / sr * 1000.0)
    bl, br = band_signals(h[:, 0], sr), band_signals(h[:, 1], sr)
    pb = bl ** 2 + br ** 2                                              # [band, n]
    bt = pb.sum(1)
    st['band_db'] = [float(v) for v in db(bt / tot)]
    valid = bt > bt.max() * 1e-5
    t20, t30, edt, late, relief = [], [], [], [], []
    ng = max(1, int(GRID / 0.005))
    for k in range(len(OCT)):
        edc, rl, _ = decay_curve(pb[k, on:], sr)
        ok = bool(valid[k])
        t20.append(rt_from(edc, 0.005, -5.0, -25.0) if ok else None)
        t30.append(rt_from(edc, 0.005, -5.0, -35.0) if ok else None)
        edt.append(rt_from(edc, 0.005, 0.0, -10.0) if ok else None)
        late.append(rl if ok else None)
        # relief relative to the impulse time, in dB re the response's total energy
        full, _, _ = decay_curve(np.concatenate([np.zeros(on), pb[k, on:]]), sr) if on else (edc, None, 0)
        relief.append((full + db(bt[k] / tot))[::ng])
    n = min(len(r) for r in relief)
    st['relief'] = np.array([r[:n] for r in relief])
    st['t20'], st['t30'], st['edt'], st['rt_late'] = t20, t30, edt, late
    bb, _, _ = decay_curve(p[on:], sr)
    st['t30_bb'] = rt_from(bb, 0.005, -5.0, -35.0)
    st['edc_bb'] = bb[::ng]
    # echo density on each channel, averaged
    nl, nr = ned_stats(h[on:, 0], sr), ned_stats(h[on:, 1], sr)
    st['ned'] = {k: (None if nl[k] is None or nr[k] is None else 0.5 * (nl[k] + nr[k])) for k in nl}
    # time slices: band balance and L/R correlation
    bal, cor, wid = [], [], []
    for a, b in SLICES:
        s0, s1 = on + int(a * sr), min(on + int(b * sr), len(h))
        if s1 - s0 < int(0.03 * sr) or pb[:, s0:s1].sum() < tot * 1e-6:
            bal.append(None), cor.append(None), wid.append(None)
            continue
        e = pb[:, s0:s1].sum(1)
        bal.append([float(v) for v in db(e / e.sum())])
        ll, rr = (bl[:, s0:s1] ** 2).sum(1), (br[:, s0:s1] ** 2).sum(1)
        lr = (bl[:, s0:s1] * br[:, s0:s1]).sum(1)
        cor.append([float(v) if e[k] > e.max() * 1e-4 else None for k, v in enumerate(lr / np.sqrt(ll * rr + 1e-300))])
        wid.append(float(db((ll + rr - 2 * lr).sum() / max((ll + rr + 2 * lr).sum(), 1e-30))))
    st['balance'], st['corr'], st['width_db'] = bal, cor, wid
    # time-variance index: the burst's answer against the one a time-invariant system would give
    st['tvi'] = None
    if probe['burst'] and probe['burst'][0] < len(wet):
        b0, b1 = probe['burst']
        m = int(min(1.3 * sr, len(wet) - b0 - lag, len(h)))
        if m > (b1 - b0):
            nf = 1 << int(np.ceil(np.log2(m + (b1 - b0) + 8)))
            tv_e, tv_l = [], []
            pred, act = [], []
            for c in (0, 1):
                pr = np.fft.irfft(np.fft.rfft(h[:m, c] / probe['amp'], nf) * np.fft.rfft(stim[b0:b1, c], nf), nf)[:m]
                pred.append(band_signals(pr, sr))
                act.append(band_signals(wet[b0 + lag:b0 + lag + m, c], sr))
            split = b1 - b0
            for k in range(len(OCT)):
                for lst, (s0, s1) in ((tv_e, (0, split)), (tv_l, (split, m))):
                    num = sum(((act[c][k, s0:s1] - pred[c][k, s0:s1]) ** 2).sum() for c in (0, 1))
                    den = sum((act[c][k, s0:s1] ** 2).sum() + (pred[c][k, s0:s1] ** 2).sum() for c in (0, 1))
                    lst.append(float(min(1.0, num / den)) if den > 0 and valid[k] else None)
            st['tvi'] = {'during': tv_e, 'after': tv_l}
    return st


def _pairs(a, b):
    return [(x, y) for x, y in zip(a, b) if x is not None and y is not None]


def impulse_score(A, B):
    """Sub-scores and their mean for LYNX's impulse stats B against the reference's A."""
    if A.get('silent') or B.get('silent'):
        return {'score': 0.0 if B.get('silent') and not A.get('silent') else None, 'parts': {}}
    d = {}
    # decay: T20 and T30 where both have them, the late-slope RT otherwise
    ds = []

    def rtd(x, y, k, key):
        return max(0.0, abs(np.log2(y / x)) - DZ[key] / np.sqrt(BW[k] * x))
    for k in range(len(OCT)):
        got = False
        for key in ('t20', 't30'):
            x, y = A[key][k], B[key][k]
            if x and y:
                ds.append(rtd(x, y, k, key))
                got = True
            elif (x is None) != (y is None) and A['rt_late'][k] and B['rt_late'][k]:
                ds.append(rtd(A['rt_late'][k], B['rt_late'][k], k, 't20'))
                got = True
        if not got and A['rt_late'][k] and B['rt_late'][k]:
            ds.append(rtd(A['rt_late'][k], B['rt_late'][k], k, 't20'))
    d['decay'] = float(np.mean(ds)) if ds else FULL['decay']
    parts = {'decay': lin(d['decay'], FULL['decay'])}
    pe = [rtd(x, y, k, 'edt') for k, (x, y) in enumerate(zip(A['edt'], B['edt'])) if x and y]
    d['edt'] = float(np.mean(pe)) if pe else FULL['edt']
    d['c80'] = max(0.0, abs(A['c80_db'] - B['c80_db']) - DZ['c80'])
    d['predelay'] = abs(A['predelay_ms'] - B['predelay_ms'])
    parts['early'] = float(np.mean([lin(d['edt'], FULL['edt']), lin(min(d['c80'], 12.0), FULL['c80']),
                                    lin(d['predelay'], FULL['predelay'])]))
    # relief: cells of the reference within 45 dB of its strongest, until its broadband curve is 40 dB down
    ra, rb = A['relief'], B['relief']
    n = min(ra.shape[1], rb.shape[1])
    stop = int(np.argmax(A['edc_bb'] <= -40.0)) or len(A['edc_bb'])
    n = max(min(n, stop + int(A['predelay_ms'] / 1000.0 / GRID) + 1), min(n, 10))
    ra, rb = ra[:, :n], rb[:, :n]
    floor = ra.max() - 45.0
    use = (ra > floor) | (rb > floor)
    dz = np.array([DZ['relief'] / np.sqrt(bw * 0.1) + 0.15 for bw in BW])[:, None]
    diff = np.maximum(np.abs(np.maximum(ra, floor) - np.maximum(rb, floor)) - dz, 0.0)
    d['relief'] = float(np.minimum(diff[use], 20.0).mean()) if use.any() else FULL['relief']
    parts['relief'] = lin(d['relief'], FULL['relief'])
    # echo density
    nd = []
    for key in ('t50', 't90'):
        x, y = A['ned'][key], B['ned'][key]
        if x is None and y is None:
            continue
        x, y = (4.0 if x is None else x), (4.0 if y is None else y)
        nd.append(lin(abs(np.log2((y + 0.005) / (x + 0.005))), FULL['ned_t']))
    if A['ned']['late'] is not None and B['ned']['late'] is not None:
        nd.append(lin(abs(A['ned']['late'] - B['ned']['late']), FULL['ned_late']))
    parts['density'] = float(np.mean(nd)) if nd else 10.0
    d['ned'] = [A['ned'], B['ned']]
    # balance and stereo per time slice
    bd, sd = [], []
    for (t0, t1), sa, sb, ca, cb in zip(SLICES, A['balance'], B['balance'], A['corr'], B['corr']):
        if sa is None:
            continue
        if sb is None:
            bd.append(FULL['balance']), sd.append(FULL['stereo'])
            continue
        nn = np.array(BW) * min(t1 - t0, 0.15)                           # independent samples per cell
        sa_, sb_ = np.array(sa), np.array(sb)
        use = sa_ > sa_.max() - 40.0
        bd.append(float(np.minimum(np.maximum(np.abs(sa_ - sb_) - DZ['balance'] / np.sqrt(nn), 0.0)[use], 20.0).mean()))
        pc = [max(0.0, abs(x - y) - DZ['corr'] / np.sqrt(nn[k])) for k, (x, y) in enumerate(zip(ca, cb))
              if x is not None and y is not None and nn[k] >= 6.0]
        if pc:
            sd.append(float(np.mean(pc)))
    d['balance'] = float(np.mean(bd)) if bd else 0.0
    d['stereo'] = float(np.mean(sd)) if sd else 0.0
    parts['balance'] = lin(d['balance'], FULL['balance'])
    parts['stereo'] = lin(d['stereo'], FULL['stereo'])
    if A.get('tvi') and B.get('tvi'):
        pm = _pairs(A['tvi']['during'] + A['tvi']['after'], B['tvi']['during'] + B['tvi']['after'])
        d['mod'] = float(np.mean([abs(x - y) for x, y in pm])) if pm else 0.0
        parts['mod'] = lin(d['mod'], FULL['mod'])
    d['level'] = max(0.0, abs(A['energy_db'] - B['energy_db']) - DZ['level'])
    parts['level'] = lin(d['level'], FULL['level'])
    ga, gb = max(abs(A['dry'][0]), abs(A['dry'][1])), max(abs(B['dry'][0]), abs(B['dry'][1]))
    if ga > 0 or gb > 0:                                                # the dry path (a Mix law) where there is one
        d['dry'] = abs(20.0 * np.log10(gb / ga)) if ga > 0 and gb > 0 else FULL['level']
        parts['dry'] = lin(max(0.0, d['dry'] - 0.1), FULL['level'])
    return {'score': agg(parts), 'parts': {k: round(v, 2) for k, v in parts.items()},
            'dist': {k: (round(v, 3) if isinstance(v, float) else v) for k, v in d.items()}}


# ------------------------------------------------------------------------------------------------ programme maps
def _stft_pow(x, win, hop):
    n = (len(x) - win) // hop + 1
    if n < 1:
        return np.zeros((0, win // 2 + 1), complex)
    idx = np.arange(win)[None, :] + hop * np.arange(n)[:, None]
    return np.fft.rfft(x[idx] * np.hanning(win), axis=1)


def third_map(x, sr, smooth=True, bands=THIRDS):
    """Third-octave energy map of a stereo signal on a 20 ms grid: [frames, bands] (L + R power). Low bands come
    from a long window, high bands from a short one; with `smooth` each band is averaged over enough frames to
    hold about four independent samples (0.04 ... 0.2 s), which is what makes the map blind to phase."""
    g = int(GRID * sr)
    nfr = len(x) // g
    out = np.zeros((nfr, len(bands)))
    edges = [(b / 2 ** (1 / 6.0), b * 2 ** (1 / 6.0)) for b in bands]
    for win, hop, lo, hi in ((4096, g, 0.0, 400.0), (1024, g // 2, 400.0, 3000.0), (256, g // 8, 3000.0, 1e9)):
        sel = [k for k, b in enumerate(bands) if lo <= b < hi]
        if not sel:
            continue
        f = np.fft.rfftfreq(win, 1.0 / sr)
        acc = None
        for c in (0, 1):
            xp = np.concatenate([np.zeros(win // 2), x[:, c], np.zeros(win)])
            pw = np.abs(_stft_pow(xp, win, hop)) ** 2
            acc = pw if acc is None else acc + pw
        per = g // hop
        m = min(nfr, len(acc) // per)
        acc = acc[:m * per].reshape(m, per, -1).mean(1)
        for k in sel:
            w = (f >= edges[k][0]) & (f < edges[k][1])
            if not w.any():
                w = np.zeros(len(f), bool)
                w[int(np.argmin(np.abs(f - bands[k])))] = True
            out[:m, k] = acc[:, w].sum(1) / (win * win)
    if smooth:
        for k, b in enumerate(bands):
            n = int(min(10, max(2, np.ceil(4.0 / (0.2316 * b * GRID)))))
            out[:, k] = np.convolve(out[:, k], np.ones(n) / n, 'same')
    return out


def corr_map(x, sr, bin_s=0.2):
    """L/R correlation per octave band (125 Hz - 8 kHz) in 200 ms bins, and the band energy: ([bins, bands], same)."""
    win, hop = 1024, 480
    L = _stft_pow(np.concatenate([x[:, 0], np.zeros(win)]), win, hop)
    R = _stft_pow(np.concatenate([x[:, 1], np.zeros(win)]), win, hop)
    f = np.fft.rfftfreq(win, 1.0 / sr)
    per = max(1, int(bin_s * sr / hop))
    m = len(L) // per
    rho, en = np.zeros((m, len(OCT) - 1)), np.zeros((m, len(OCT) - 1))
    for k, c in enumerate(OCT[1:]):
        w = (f >= c / 2 ** 0.5) & (f < c * 2 ** 0.5)
        ll = (np.abs(L[:m * per, w]) ** 2).sum(1).reshape(m, per).sum(1)
        rr = (np.abs(R[:m * per, w]) ** 2).sum(1).reshape(m, per).sum(1)
        lr = (L[:m * per, w] * np.conj(R[:m * per, w])).real.sum(1).reshape(m, per).sum(1)
        rho[:, k] = lr / np.sqrt(ll * rr + 1e-300)
        en[:, k] = ll + rr
    return rho, en


def mod_spectrum(series, ok, hop_s=GRID):
    """Mean power of the fluctuation of each column of `series` ([frames, bands]; `ok` marks usable cells) in
    octave bins of modulation frequency 0.25 ... 16 Hz. Phase-blind by construction."""
    edges = [0.25 * 2 ** k for k in range(7)]
    acc, cnt = np.zeros(6), 0
    n = series.shape[0]
    if n < 64:
        return None
    fr = np.fft.rfftfreq(n, hop_s)
    for k in range(series.shape[1]):
        good = ok[:, k]
        if good.sum() < n * 0.5:
            continue
        s = np.interp(np.arange(n), np.flatnonzero(good), series[good, k])
        s = (s - s.mean()) * np.hanning(n)
        sp = np.abs(np.fft.rfft(s)) ** 2 / (np.hanning(n) ** 2).sum()
        acc += np.array([sp[(fr >= edges[i]) & (fr < edges[i + 1])].sum() for i in range(6)])
        cnt += 1
    return acc / cnt if cnt else None


def programme_stats(out, stim, sr, g=(0.0, 0.0, 0), kind='verb'):
    """Measurements of a programme render. kind verb: the wet part's long-term third-octave spectrum, its level
    over time in three wide bands and the L/R correlation per octave band - statistics that hold for any phase
    of a diffuse tail (a fine time-frequency map does not: a held note through a reverb lands on a different mode
    gain in every realisation). kind delay / mod: the fine third-octave x 20 ms map, the effect being deterministic."""
    wet = wet_of(out, stim, g) if kind != 'mod' else out
    raw = third_map(wet, sr, smooth=False)
    st = {'energy_db': float(db((wet ** 2).sum())), 'lt': db(raw.sum(0))}
    wide = np.stack([raw[:, :8].sum(1), raw[:, 8:18].sum(1), raw[:, 18:].sum(1)], 1)       # < 300 Hz, - 3 kHz, above
    st['wide'] = db(np.apply_along_axis(lambda v: np.convolve(v, np.ones(8) / 8.0, 'same'), 0, wide))
    rho, en = corr_map(wet, sr)
    st['rho_band'] = (rho * en).sum(0) / np.maximum(en.sum(0), 1e-300)
    st['en_band'] = en.sum(0)
    if kind != 'verb':
        st['map'] = third_map(wet, sr)
    if kind == 'delay':
        st['rho'], st['rho_en'] = rho, en
    if kind == 'mod':
        sel = [k for k, b in enumerate(THIRDS) if 400.0 <= b <= 8000.0]
        raw = raw[:, sel]
        n = min(len(raw), len(stim) // int(GRID * sr))
        src = third_map(stim[:n * int(GRID * sr)], sr, smooth=False)[:n, sel]
        raw = raw[:n]
        k2 = np.ones(2) / 2.0
        sm = lambda m: np.apply_along_axis(lambda v: np.convolve(v, k2, 'same'), 0, m)
        raw, src = sm(raw), sm(src)
        ok = src > src.max() * 1e-4
        st['motion_t'] = mod_spectrum(db(raw) - db(src), ok)
        chan = []
        for c in (0, 1):
            one = np.stack([out[:, c], out[:, c]], 1)
            chan.append(sm(third_map(one, sr, smooth=False)[:n, sel]))
        okp = (chan[0] + chan[1]) > (chan[0] + chan[1]).max() * 1e-4
        st['motion_p'] = mod_spectrum(db(chan[0]) - db(chan[1]), okp)
    return st


def programme_score(A, B, kind='verb'):
    parts, d = {}, {}
    if kind == 'verb':
        use = A['lt'] > A['lt'].max() - 40.0
        dl = float((B['lt'] - A['lt'])[use].mean())                     # level: mean of the band differences
        d['spec'] = max(0.0, float(np.abs(B['lt'] - dl - A['lt'])[use].mean()) - DZ['p_spec'])
        n = min(len(A['wide']), len(B['wide']))
        wa, wb = A['wide'][:n], B['wide'][:n] - dl
        floor = wa.max() - 40.0
        u = (wa > floor) | (wb > floor)
        diff = np.minimum(np.abs(np.maximum(wa, floor) - np.maximum(wb, floor)), 20.0)
        d['env'] = max(0.0, float(diff[u].mean()) - DZ['p_env']) if u.any() else 0.0
        d['level'] = max(0.0, abs(dl) - DZ['p_level'])
        ub = A['en_band'] > A['en_band'].max() * 1e-4
        ub[:2] = False                                                  # 125 / 250 Hz: too few partials in programme
        d['stereo'] = max(0.0, float(np.abs(A['rho_band'] - B['rho_band'])[ub].mean()) - DZ['p_stereo']) if ub.any() else 0.0
        parts = {'spec': lin(d['spec'], P_FULL), 'env': lin(d['env'], P_FULL), 'level': lin(d['level'], FULL['level']),
                 'stereo': lin(d['stereo'], FULL['stereo'])}
    else:
        ma, mb = db(A['map']), db(B['map'])
        n = min(len(ma), len(mb))
        ma, mb = ma[:n], mb[:n]
        dl = B['energy_db'] - A['energy_db']
        floor = ma.max() - 50.0
        mb2 = mb - dl
        use = (ma > floor) | (mb2 > floor)
        diff = np.abs(np.maximum(ma, floor) - np.maximum(mb2, floor))
        d['spec'] = float(np.minimum(diff[use], 20.0).mean()) if use.any() else 0.0
        d['level'] = abs(dl)
        parts['spec'] = lin(d['spec'], FULL['spec'])
        parts['level'] = lin(d['level'], FULL['level'])
        if kind == 'delay':
            m = min(len(A['rho']), len(B['rho']))
            use = A['rho_en'][:m] > A['rho_en'].max() * 1e-4
            d['stereo'] = float(np.abs(A['rho'][:m] - B['rho'][:m])[use].mean()) if use.any() else 0.0
        else:                                                           # mod: per band over the whole file
            ub = A['en_band'] > A['en_band'].max() * 1e-4
            d['stereo'] = float(np.abs(A['rho_band'] - B['rho_band'])[ub].mean()) if ub.any() else 0.0
        parts['stereo'] = lin(d['stereo'], FULL['stereo'])
    if kind == 'mod':
        ms = []
        for key in ('motion_t', 'motion_p'):
            x, y = A.get(key), B.get(key)
            if x is None or y is None:
                continue
            # dB^2 power of the fluctuation per modulation octave; below 0.01 dB^2 both count as still
            ms.append(float(np.abs(db(x + 0.01) - db(y + 0.01)).mean()))
        if ms:
            d['motion'] = float(np.mean(ms))
            parts['motion'] = lin(d['motion'], FULL['motion'])
    return {'score': agg(parts), 'parts': {k: round(v, 2) for k, v in parts.items()},
            'dist': {k: round(v, 3) for k, v in d.items()}}


# ------------------------------------------------------------------------------------------------ pilot tone
def pilot_stats(x, sr, hz, t0=1.0, t1=None):
    """Modulation seen on a steady pilot tone of `hz` (mono mix of x between t0 and t1 s): the tone is brought to
    0 Hz and low-passed at 60 Hz; the phase gives the instantaneous frequency. Returns the rate (Hz, strongest
    line 0.1-30 Hz of the frequency deviation, or of the level if the frequency is still), depth (cents, peak:
    1.414 x rms), level wobble (dB rms). A time-invariant effect gives depth 0 and wobble 0."""
    m = x.mean(1) if x.ndim == 2 else x
    a, b = int(t0 * sr), int((t1 if t1 else len(m) / sr - 0.5) * sr)
    seg = m[a:b]
    n = len(seg)
    z = seg * np.exp(-2j * np.pi * hz * (np.arange(n) / sr))
    Z = np.fft.fft(z)
    f = np.fft.fftfreq(n, 1.0 / sr)
    Z[np.abs(f) > 60.0] = 0
    z = np.fft.ifft(Z)
    dec = max(1, sr // 400)
    z = z[sr // 4:n - sr // 4:dec]
    fs = sr / dec
    amp = np.abs(z)
    if amp.max() <= 1e-12:
        return {'rate_hz': None, 'depth_cents': 0.0, 'am_db': 0.0}
    dev = np.diff(np.unwrap(np.angle(z))) * fs / (2 * np.pi)
    dev = dev - dev.mean()
    cents = 1200.0 * np.log2(np.maximum(1.0 + dev / hz, 1e-6))
    lvl = 20 * np.log10(np.maximum(amp / amp.mean(), 1e-9))
    use = cents if np.sqrt(np.mean(cents ** 2)) > 0.02 else lvl - lvl.mean()
    sp = np.abs(np.fft.rfft(use * np.hanning(len(use))))
    fr = np.fft.rfftfreq(len(use), 1.0 / fs)
    band = (fr >= 0.1) & (fr <= 30.0)
    rate = float(fr[band][int(np.argmax(sp[band]))]) if band.any() and sp[band].max() > 1e-9 else None
    return {'rate_hz': rate, 'depth_cents': float(np.sqrt(2.0 * np.mean(cents ** 2))), 'am_db': float(np.std(lvl))}


# ------------------------------------------------------------------------------------------------ files
def write_wav(path, x, sr):
    import struct
    y = np.asarray(x, np.float32)
    if y.ndim == 1:
        y = np.stack([y, y], 1)
    dd = y.tobytes()
    h = b'RIFF' + struct.pack('<I', 36 + len(dd)) + b'WAVE' + b'fmt ' + struct.pack('<IHHIIHH', 16, 3, 2, sr, sr * 8, 8, 32) + \
        b'data' + struct.pack('<I', len(dd))
    open(path, 'wb').write(h + dd)


def _clean(o):
    if isinstance(o, dict):
        return {k: _clean(v) for k, v in o.items() if not isinstance(v, np.ndarray)}
    if isinstance(o, (list, tuple)):
        return [_clean(v) for v in o]
    if isinstance(o, (np.floating, np.integer)):
        return float(o)
    return o


def pair(ref, lynx, stim, kind='verb', g_ref=None, g_lynx=None):
    """Tail score of one file pair. An impulse stimulus gives the impulse half, anything else a programme file."""
    a, sr = fxparity.read_wav(ref)
    b, _ = fxparity.read_wav(lynx)
    s, _ = fxparity.read_wav(stim)
    probe = find_probe(s, sr)
    if probe is not None:
        A, B = impulse_stats(a, s, sr, g_ref), impulse_stats(b, s, sr, g_lynx)
        r = impulse_score(A, B)
        r['type'] = 'impulse'
        r['ref'], r['lynx'] = _clean(A), _clean(B)
    else:
        A = programme_stats(a, s, sr, g_ref or (0.0, 0.0, 0), kind)
        B = programme_stats(b, s, sr, g_lynx or (0.0, 0.0, 0), kind)
        r = programme_score(A, B, kind)
        r['type'] = 'programme'
    return r


def score_dir(refdir, lynxdir, stimdir, kind='verb', wave=True):
    """Every <setting>_<signal>.wav of both folders. Returns {'files': {...}, 'settings': {...}, 'tail', 'wave',
    'parts': mean sub-scores, 'loss': points each part costs on the row's tail score}."""
    names = sorted(f[:-4] for f in os.listdir(refdir) if f.endswith('.wav') and os.path.exists(os.path.join(lynxdir, f)))
    sets = {}
    for nm in names:
        s, _, sig = nm.partition('_')
        if os.path.exists(os.path.join(stimdir, sig + '.wav')):
            sets.setdefault(s, []).append(sig)
    res = {'files': {}, 'settings': {}}
    for s, sigs in sets.items():
        gr = gl = None
        imp = [x for x in sigs if find_probe(*fxparity.read_wav(os.path.join(stimdir, x + ".wav"))) is not None]
        if imp and kind != 'mod':
            st, sr = fxparity.read_wav(os.path.join(stimdir, imp[0] + '.wav'))
            gr = dry_path(fxparity.read_wav(os.path.join(refdir, f'{s}_{imp[0]}.wav'))[0], st, sr)
            gl = dry_path(fxparity.read_wav(os.path.join(lynxdir, f'{s}_{imp[0]}.wav'))[0], st, sr)
        half = {'impulse': [], 'programme': []}
        for sig in sigs:
            a, b = os.path.join(refdir, f'{s}_{sig}.wav'), os.path.join(lynxdir, f'{s}_{sig}.wav')
            r = pair(a, b, os.path.join(stimdir, sig + '.wav'), kind, gr, gl)
            if wave:
                w = fxparity.pair(a, b)
                r['wave'] = w['score']
                r['wave_detail'] = {k: (round(w[k], 2) if w.get(k) is not None else None) for k in ('esr_db', 'rms_db', 'spec_db', 'env_corr')}
            res['files'][f'{s}_{sig}'] = r
            if r['score'] is not None:
                half[r['type']].append(r['score'])
        hs = [float(np.mean(v)) for v in half.values() if v]
        res['settings'][s] = {'tail': round(float(np.mean(hs)), 3) if hs else None,
                              'impulse': round(float(np.mean(half['impulse'])), 3) if half['impulse'] else None,
                              'programme': round(float(np.mean(half['programme'])), 3) if half['programme'] else None,
                              'dry_ref': gr, 'dry_lynx': gl}
    ts = [v['tail'] for v in res['settings'].values() if v['tail'] is not None]
    res['tail'] = round(float(np.mean(ts)), 2) if ts else None
    ws = [r['wave'] for r in res['files'].values() if r.get('wave') is not None]
    res['wave'] = round(float(np.mean(ws)), 2) if ws else None
    # mean sub-score per part, and the points each part costs on the row's tail score: a file's loss (10 - score)
    # split over its parts in proportion to their squared deficits, averaged like the score itself
    parts, loss = {}, {}
    for s, sigs in sets.items():
        fs = [res['files'][f'{s}_{sig}'] for sig in sigs]
        types = {r['type'] for r in fs if r['score'] is not None}
        for typ in types:
            grp = [r for r in fs if r['type'] == typ and r['score'] is not None]
            for r in grp:
                df = {k: (10.0 - v) ** 2 for k, v in r['parts'].items()}
                tot = sum(df.values())
                for k, v in df.items():
                    share = (10.0 - r['score']) * (v / tot if tot > 0 else 0.0)
                    loss[f'{typ[:3]}.{k}'] = loss.get(f'{typ[:3]}.{k}', 0.0) + share / len(grp) / len(types) / max(1, len(ts))
    for typ in ('impulse', 'programme'):
        fs = [r for r in res['files'].values() if r['type'] == typ and r['score'] is not None]
        for k in sorted({k for r in fs for k in r['parts']}):
            parts[f'{typ[:3]}.{k}'] = round(float(np.mean([r['parts'][k] for r in fs if k in r['parts']])), 2)
    res['parts'] = parts
    res['loss'] = {k: round(v, 3) for k, v in sorted(loss.items(), key=lambda kv: -kv[1])}
    res['version'] = VERSION
    return res


# ------------------------------------------------------------------------------------------------ probes / engine
def make_probes(outdir, sr=48000):
    os.makedirs(outdir, exist_ok=True)
    rng = np.random.default_rng(190)
    burst = rng.standard_normal(int(0.3 * sr)) * 0.25
    for name, amp in (('imp06', 0.5), ('imp20', 0.1), ('imp40', 0.01)):
        x = np.zeros(8 * sr)
        x[int(0.1 * sr)] = amp
        x[4 * sr:4 * sr + len(burst)] = burst * (amp / 0.5)
        write_wav(os.path.join(outdir, name + '.wav'), x, sr)
    t = np.arange(10 * sr) / sr
    fade = np.clip(t / 0.05, 0, 1) * np.clip((10 - t) / 0.05, 0, 1)
    for hz in (1000, 5000):
        write_wav(os.path.join(outdir, f'pilot{hz}.wav'), 0.25 * np.sin(2 * np.pi * hz * t) * fade, sr)
    w = rng.standard_normal(6 * sr)
    W = np.fft.rfft(w)
    W[1:] /= np.sqrt(np.arange(1, len(W)))
    pk = np.fft.irfft(W, len(w))
    pk = np.concatenate([pk / np.abs(pk).max() * 0.5, np.zeros(4 * sr)])
    write_wav(os.path.join(outdir, 'pink.wav'), pk, sr)


def engine_table(fxr, effect, probedir, outdir, opts):
    """Renders the probes through one LYNX effect (tools/fxrender) and returns its lab measurements."""
    import subprocess
    os.makedirs(outdir, exist_ok=True)
    names = ['imp06', 'imp20', 'imp40', 'pilot1000', 'pilot5000', 'pink']
    files = []
    for n in names:
        files += [os.path.join(probedir, n + '.wav'), os.path.join(outdir, n + '.wav')]
    r = subprocess.run([fxr, effect] + list(opts) + files, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout[-300:] + r.stderr[-300:])
    res = {}
    for n in ('imp06', 'imp20', 'imp40'):
        s, sr = fxparity.read_wav(os.path.join(probedir, n + '.wav'))
        o, _ = fxparity.read_wav(os.path.join(outdir, n + '.wav'))
        res[n] = _clean(impulse_stats(o, s, sr))
    for hz in (1000, 5000):
        o, sr = fxparity.read_wav(os.path.join(outdir, f'pilot{hz}.wav'))
        res[f'pilot{hz}'] = pilot_stats(o, sr, hz, 2.0, 9.0)
    return res


def _fmt(v, f='%.2f'):
    return '-' if v is None else f % v


def print_engine(res):
    a = res['imp06']
    if a.get('silent'):
        print('silent')
        return
    print('band   ' + ' '.join(f'{c:>7d}' for c in OCT))
    for key in ('t20', 't30', 'edt', 'rt_late'):
        print(f'{key:7s}' + ' '.join(f'{_fmt(v):>7s}' for v in a[key]))
    print('level  ' + ' '.join(f'{v:7.1f}' for v in a['band_db']))
    if a.get('tvi'):
        print('tvi dur' + ' '.join(f'{_fmt(v):>7s}' for v in a['tvi']['during']))
        print('tvi aft' + ' '.join(f'{_fmt(v):>7s}' for v in a['tvi']['after']))
    for i, sl in enumerate(SLICES):
        if a['corr'][i]:
            print(f'corr {sl[0]:.2f}-{sl[1]:.1f}s ' + ' '.join(f'{_fmt(v):>6s}' for v in a['corr'][i]) + f'  width {a["width_db"][i]:+.1f} dB')
    print(f"pre-delay {a['predelay_ms']:.1f} ms  C80 {a['c80_db']:+.1f} dB  centre {a['centre_ms']:.0f} ms  energy {a['energy_db']:+.1f} dB  "
          f"NED t50 {_fmt(a['ned']['t50'], '%.3f')} t90 {_fmt(a['ned']['t90'], '%.3f')} late {_fmt(a['ned']['late'])}")
    for n in ('imp20', 'imp40'):
        b = res[n]
        if not b.get('silent'):
            print(f"{n}: energy {b['energy_db']:+.1f} dB (re its impulse)  T30 broadband {_fmt(b['t30_bb'])} s (imp06 {_fmt(a['t30_bb'])} s)")
    for hz in (1000, 5000):
        p = res[f'pilot{hz}']
        print(f"pilot {hz} Hz: rate {_fmt(p['rate_hz'])} Hz  depth {p['depth_cents']:.2f} cents  level wobble {p['am_db']:.2f} dB")


def main(argv):
    def opt(name, default=None):
        if name in argv:
            i = argv.index(name)
            v = argv[i + 1]
            del argv[i:i + 2]
            return v
        return default
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == 'selftest':
        import taillab_test
        return taillab_test.run()
    kind, stim, js = opt('--kind', 'verb'), opt('--stim'), opt('--json')
    if cmd == 'pair' and len(argv) >= 4 and stim:
        dry = opt('--dry')
        g = tuple(float(v) for v in dry.split(',')) if dry else None
        print(json.dumps(_clean(pair(argv[2], argv[3], stim, kind, g, g))))
    elif cmd == 'dir' and len(argv) >= 4 and stim:
        res = score_dir(argv[2], argv[3], stim, kind)
        for nm, r in res['files'].items():
            print(f"{nm:22s} wave {r.get('wave')}  tail {r['score']}  " + ' '.join(f'{k}={v}' for k, v in r['parts'].items()))
        for s, v in res['settings'].items():
            print(f"setting {s}: tail {v['tail']} (impulse {v['impulse']}, programme {v['programme']})")
        print(f"ROW wave {res['wave']} tail {res['tail']}  loss " + ' '.join(f'{k}={v}' for k, v in res['loss'].items()))
        if js:
            json.dump(_clean(res), open(js, 'w'))
    elif cmd == 'stats' and len(argv) >= 3 and stim:
        o, sr = fxparity.read_wav(argv[2])
        s, _ = fxparity.read_wav(stim)
        print(json.dumps(_clean(impulse_stats(o, s, sr))))
    elif cmd == 'pilot' and len(argv) >= 4:
        o, sr = fxparity.read_wav(argv[2])
        print(json.dumps(pilot_stats(o, sr, float(argv[3]), *(float(v) for v in argv[4:6]))))
    elif cmd == 'probes' and len(argv) >= 3:
        make_probes(argv[2], int(argv[3]) if len(argv) > 3 else 48000)
    elif cmd == 'engine' and len(argv) >= 6:
        res = engine_table(argv[2], argv[3], argv[4], argv[5], argv[6:])
        print_engine(res)
        if js:
            json.dump(res, open(js, 'w'))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
