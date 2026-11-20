#!/usr/bin/env python3
"""timbre_score.py - the TIMBRE SCORE of a single note (round 190ni, the quality gate of LYNX Model).

The waveform score (scripts/fxparity.py score(), untouched) needs a waveform null. A physical model never nulls
against a different recording (phases, the room, the take's noise differ), so the wave score under-rates a good
model: the 190nd prototype's piano scored 4/10 at a 1.0 dB third-octave spectrum difference. This score compares
what a listener hears in ONE note, blind to phase, and is always reported next to the wave score, never instead.

  timbre_score.py pair <ref.wav> <cand.wav> [--f0 HZ] [--dur S] [--partials K] [--json OUT]
        one note: the measurements, the sub-scores and the timbre score (0-10), plus the wave metrics (JSON)
  timbre_score.py selftest
        the unit tests (tools/physmod/test_timbre_score.py) on synthetic notes with known answers

Both notes are cut at their onsets (modal_analysis.onset), the candidate then shifted (+-8 ms) to where its 2 ms
envelope best correlates with the reference's (by more than 0.02 over no shift) (two onset detections disagree by a few ms on a soft start), and both
compared over the shorter length (at most --dur, 8 s).
Parts (each 10 at a match, 0 at the distance FULL[part]; the score is 10 minus the weighted quadratic mean of what
the parts miss, as scripts/taillab.py's tail score):
  pitch      cents error of the reference's main partial (the strongest; with --f0 the strongest near f0)
  inharm     the partials' frequency pattern: cents error of each partial relative to the main one (weighted)
  levels     each partial's initial level relative to the main one (dB, weighted)
  decay      each partial's T60, |log2 ratio| (weighted)
  spectime   spectral envelope over time: third-octave energy on a log-spaced time grid (0-10-30-60-100-200-400 ms
             ... doubling), dB, overall level removed; the cells audible in either note (within 50 dB of the top and
             6 dB above the reference's background in that band: its quietest slice, the room and the take's noise)
  attack     attack shape: 10-90 % rise time (|log2|), spectral centroid of the first 30 ms (|log2|) and the 1 ms
             envelope over the first 150 ms (dB, each normalised to its peak, on a log time grid 0.5-150 ms; band-wise, so
             blind to the phases between octaves). The rise ends at the strike's own peak (the first local maximum
             of at least half the 150 ms maximum)
  level      loudness difference of the first 2 s (dB; weight 0.5)
Partial weights are sqrt(energy) (energy = a0^2 * T60) of the reference's K strongest partials (default 16); a
partial the candidate lacks (no peak within 60 cents and 40 dB of it) misses fully in pitch / inharm / levels / decay.
The reference's stiff-string inharmonicity B (f_n = n f0 sqrt(1 + B n^2), least squares) is reported for both notes
when the partials are near-harmonic; it is part of `inharm` through the partial pattern, not a separate part.
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import modal_analysis as ma  # noqa: E402

FULL = {'pitch': 25.0, 'inharm': 30.0, 'levels': 12.0, 'decay': 1.0, 'spectime': 10.0, 'attack': 1.0, 'level': 6.0}
WEIGHT = {'pitch': 1.0, 'inharm': 1.0, 'levels': 1.0, 'decay': 1.0, 'spectime': 1.5, 'attack': 1.0, 'level': 0.5}
ATTACK_FULL = {'rise': 1.5, 'centroid': 1.0, 'curve': 8.0}
MATCH_CENTS = 60.0
MISSING_DB = 24.0  # a candidate partial this far below the reference's (level corrected) counts as missing
MASK_DB = 30.0
FLOOR_DB = 50.0  # reference partials further below the strongest one are not scored


FLOOR_MARGIN = 6.0


def band_floor(grid):
    """per band (rows) the background level of a note's band-energy grid: its quietest time slice, as a column"""
    return grid.min(axis=1, keepdims=True)


def spectime(gr, gc, lev, rule='190nl'):
    """the spectro-temporal part from the two notes' band-energy grids (dB; rows = third-octave bands, columns = time
    slices) and the candidate's level offset `lev` (dB): {'miss' 0..1, 'db' mean dB difference or None}.
    A cell is part of the note where it stands within 50 dB of the top and FLOOR_MARGIN dB above the reference's
    background in that band (its quietest slice: the room, the take's noise) in either note.
    Rule '190nl' (round 190nl): (1) the overall offset removed is the median difference over the cells audible in BOTH
    notes (190ni took it over every audible cell, so a clean note against a noisy take - every cell the room fills and
    the note leaves empty reads 30 dB down - had its real partials pushed up by the room's level); (2) a candidate cell
    below the reference's background reads as that background (what the take's room would let one hear of it: a clean
    synthesis is not missing the room's rumble). Rule '190ni': the score as round 190ni shipped it (reported beside)."""
    top = max(gr.max(), gc.max() - lev)
    floor = band_floor(gr)
    ra = (gr > top - 50) & (gr > floor + FLOOR_MARGIN)
    ca = (gc - lev > top - 50) & (gc - lev > floor + FLOOR_MARGIN)
    aud = ra | ca
    if not aud.any():
        return {'miss': 1.0, 'db': None}
    if rule == '190ni':
        d = (gc - lev) - gr
        d = d - np.median(d[aud])
    else:
        both = ra & ca
        d = np.maximum(gc - lev, floor) - gr
        d = d - np.median(d[both] if both.sum() >= 3 else d[aud])
    v = float(np.mean(np.minimum(np.abs(d[aud]), 30.0)))
    return {'miss': v / FULL['spectime'], 'db': round(v, 2)}


def cents(a, b):
    return 1200.0 * np.log2(a / b)


def time_edges(n_seconds):
    e = [0.0, 0.01, 0.03, 0.06, 0.1, 0.2, 0.4]
    while e[-1] * 2 <= n_seconds + 1e-9:
        e.append(e[-1] * 2)
    if n_seconds - e[-1] > 0.1:
        e.append(n_seconds)
    return np.array(e)


def stiff_b(freqs):
    """(f0, B) of the stiff-string law fitted to near-harmonic partials, or None"""
    fs = np.sort(np.asarray(freqs))
    if len(fs) < 4:
        return None
    f1 = fs[0]
    n = np.round(fs / f1)
    ok = (n >= 1) & (np.abs(cents(fs, n * f1)) < 80)
    if ok.sum() < 4 or len(set(n[ok])) < 4:
        return None
    A = np.vstack([np.ones(ok.sum()), n[ok] ** 2]).T
    sol, *_ = np.linalg.lstsq(A, (fs[ok] / n[ok]) ** 2, rcond=None)
    if sol[0] <= 0:
        return None
    return float(np.sqrt(sol[0])), float(max(sol[1] / sol[0], 0.0))


def envelope_db(x, sr, ms=1.0, span=0.15):
    e = ma.band_env(x, sr, ms, span)
    return 20.0 * np.log10(e / max(e.max(), 1e-12) + 1e-9)


def envelope_lag(a, b, sr, span=0.08, search=0.008, margin=0.02):
    """samples b lags a (b[i + lag] ~ a[i]): the best Pearson correlation of their 2 ms envelopes over the first
    `span` s, within +-`search` s (two independent onset detections differ by a few ms on a soft or noisy start). Lag 0
    stays unless another lag correlates better by more than `margin` (phase ripple must not move a note)."""
    n = int(span * sr)
    h = int(search * sr)
    ea = ma.smooth_env(np.asarray(a[:n], np.float64), sr, 2.0)
    eb = ma.smooth_env(np.concatenate([np.asarray(b[:n + h], np.float64), np.zeros(max(0, n + h - len(b)))]), sr, 2.0)

    def corr(L):
        seg = eb[max(L, 0):max(L, 0) + n - max(-L, 0)]
        ref = ea[max(-L, 0):max(-L, 0) + len(seg)]
        if len(seg) < 16 or seg.std() == 0 or ref.std() == 0:
            return -1.0
        return float(np.corrcoef(ref, seg)[0, 1])

    zero = corr(0)
    best, lag = zero, 0
    for L in range(-h, h + 1):
        v = corr(L)
        if v > best:
            best, lag = v, L
    return lag if best - zero > margin else 0


def align(r, c, sr):
    """c shifted so its envelope lines up with r's (phase-blind; see envelope_lag)"""
    lag = envelope_lag(r, c, sr)
    if lag > 0:
        return c[lag:]
    if lag < 0:
        return np.concatenate([np.zeros(-lag), c])
    return c


def prepare(x, sr, dur):
    on = ma.onset(x, sr)
    return x[on:on + int(dur * sr)]


def attack_part(r, c, sr):
    """the attack part of two aligned notes: (miss 0..1, measures) - see the module doc"""
    rr, rc = ma.rise_time(r, sr), ma.rise_time(c, sr)
    a30 = int(0.03 * sr)
    cr, cc = ma.centroid(r[:a30], sr), ma.centroid(c[:a30], sr)
    er, ec = envelope_db(r, sr), envelope_db(c, sr)
    k = min(len(er), len(ec))
    # (on a log time grid 0.5 ... 150 ms: the first milliseconds weigh as much as the rest)
    grid = np.unique(np.clip((np.geomspace(0.0005, 0.15, 32) * sr).astype(int), 0, k - 1))
    er, ec = er[grid], ec[grid]
    m = er > -40
    curve = float(np.mean(np.minimum(np.abs(er[m] - ec[m]), 30.0))) if m.any() else 30.0
    am = {'rise': abs(np.log2(max(rc, 1e-5) / max(rr, 1e-5))) / ATTACK_FULL['rise'],
          'centroid': abs(np.log2(max(cc, 1.0) / max(cr, 1.0))) / ATTACK_FULL['centroid'],
          'curve': curve / ATTACK_FULL['curve']}
    return float(np.mean([min(v, 1.0) for v in am.values()])), {
        'rise_ms': [round(rr * 1000, 3), round(rc * 1000, 3)], 'centroid_hz': [round(cr, 1), round(cc, 1)], 'curve_db': round(curve, 2)}


class StrikeLoss:
    """the parts of the score a strike's parameters move (round 190nl, tools/physmod/refine.py): the attack and the
    spectro-temporal cells of the first `span` s, against one reference note, the reference analysed once.
    loss(cand) = (WEIGHT spectime * spectime miss^2 + attack miss^2) / (WEIGHT spectime + 1), the score's own parts;
    the candidate's level offset is taken over the first 2 s as the score does (render at least that long)."""

    def __init__(self, ref, sr, dur=8.0, span=0.4):
        self.sr = sr
        self.r = prepare(np.asarray(ref, np.float64), sr, dur)
        self.span = span
        self.edges = time_edges(len(self.r) / sr)
        self.gr = ma.band_energy_grid(self.r, sr, self.edges)
        self.cols = int(np.searchsorted(self.edges, span + 1e-9)) - 1

    def loss(self, cand, detail=False):
        sr = self.sr
        c = prepare(np.asarray(cand, np.float64), sr, len(self.r) / sr)
        c = align(self.r, c, sr)
        n = min(len(self.r), len(c))
        r = self.r[:n]
        c = c[:n]
        m2 = int(min(n, 2 * sr))
        lev = 10 * np.log10((np.mean(c[:m2] ** 2) + 1e-30) / (np.mean(r[:m2] ** 2) + 1e-30))
        e = self.edges[:self.cols + 1]
        gc = ma.band_energy_grid(c, sr, e)
        # (the reference's top and background from its whole note, as score_pair sees them)
        top = max(self.gr.max(), gc.max() - lev)
        floor = band_floor(self.gr)
        gr = self.gr[:, :self.cols]
        ra = (gr > top - 50) & (gr > floor + FLOOR_MARGIN)
        ca = (gc - lev > top - 50) & (gc - lev > floor + FLOOR_MARGIN)
        aud, both = ra | ca, ra & ca
        if aud.any():
            d = np.maximum(gc - lev, floor) - gr
            d = d - np.median(d[both] if both.sum() >= 3 else d[aud])
            spec = float(np.mean(np.minimum(np.abs(d[aud]), 30.0))) / FULL['spectime']
        else:
            spec = 1.0
        att, info = attack_part(r, c, sr)
        w = WEIGHT['spectime']
        loss = (w * min(spec, 1.0) ** 2 + att ** 2) / (w + 1.0)
        return (loss, {'spec': spec, 'attack': att, **info}) if detail else loss


def score_pair(ref, cand, sr, f0=None, dur=8.0, partials=16, wave=True):
    """the timbre score of `cand` against `ref` (mono float arrays at the same rate): a dict (see the module doc)"""
    r = prepare(np.asarray(ref, np.float64), sr, dur)
    c = prepare(np.asarray(cand, np.float64), sr, dur)
    c = align(r, c, sr)
    n = min(len(r), len(c))
    r, c = r[:n], c[:n]
    secs = n / sr
    out = {'seconds': round(secs, 3)}
    miss = {}

    # level
    m2 = int(min(n, 2 * sr))
    lev = 10 * np.log10((np.mean(c[:m2] ** 2) + 1e-30) / (np.mean(r[:m2] ** 2) + 1e-30))
    out['level_db'] = round(float(lev), 2)
    miss['level'] = abs(lev) / FULL['level']

    # partials
    fmin = 0.8 * f0 if f0 else 40.0
    rm = ma.analyse_modes(r, sr, fmin=fmin, k=max(partials * 2, 24))
    top_a = max([m['a0'] for m in rm] or [0.0])
    rm = [m for m in rm if m['a0'] >= top_a * 10 ** (-FLOOR_DB / 20.0)]
    # masking: a partial far below a stronger one within about a critical band (+-max(100 Hz, 15 %)) is not heard
    rm = [m for m in rm if not any(o['a0'] > m['a0'] * 10 ** (MASK_DB / 20.0) and abs(o['f'] - m['f']) < max(100.0, 0.15 * m['f'])
                                   for o in rm)][:partials]
    if not rm:
        raise ValueError('no partials found in the reference')
    w = np.sqrt(np.array([m['energy'] for m in rm]))
    w = w / w.sum()
    hop = max(1, int(sr * 0.005))
    rows = []
    for m in rm:
        others = [abs(cents(o['f'], m['f'])) for o in rm if o is not m]
        span = min([MATCH_CENTS] + [0.45 * d for d in others])
        pk = ma.peak_near(c, sr, m['f'], span, ma.WINDOWS[m['window']])
        best = (cents(pk[0], m['f']), pk[0]) if pk is not None else None
        row = {'f': round(m['f'], 2), 'ref_t60': round(m['t60'], 3), 'ref_db': round(20 * np.log10(m['a0'] + 1e-12), 2)}
        if best is not None:
            t, a = ma.track(c, sr, best[1], m['win'], hop)
            p = ma.fit_decay(t, a)
            if p is not None and 20 * np.log10((p['a0'] + 1e-12) / (m['a0'] + 1e-12)) > -MISSING_DB - abs(lev):
                row.update({'cand_f': round(best[1], 2), 'cents': round(best[0], 2), 'cand_t60': round(p['t60'], 3),
                            'cand_db': round(20 * np.log10(p['a0'] + 1e-12), 2)})
        rows.append(row)
    main = 0
    if f0:
        near = [i for i, m in enumerate(rm[:6]) if abs(cents(m['f'], f0)) < 100 or abs(cents(m['f'], 2 * f0)) < 100]
        main = near[0] if near else 0
    mr = rows[main]
    found = ['cents' in q for q in rows]
    out['main_partial_hz'] = mr['f']
    out['partials_matched'] = '%d/%d' % (sum(found), len(rows))
    miss['pitch'] = abs(mr['cents']) / FULL['pitch'] if found[main] else 1.0
    out['pitch_cents'] = mr.get('cents')
    inh, lv, dc = [], [], []
    for i, q in enumerate(rows):
        if not found[i]:
            inh.append(1.0); lv.append(1.0); dc.append(1.0)
            continue
        dc.append(abs(np.log2(q['cand_t60'] / q['ref_t60'])) / FULL['decay'])
        if i == main or not found[main]:
            inh.append(0.0 if i == main else 1.0)
            lv.append(0.0 if i == main else 1.0)
            continue
        inh.append(abs(q['cents'] - mr['cents']) / FULL['inharm'])
        lv.append(abs((q['cand_db'] - mr['cand_db']) - (q['ref_db'] - mr['ref_db'])) / FULL['levels'])
    clip = lambda v: np.minimum(np.asarray(v), 1.0)
    # (the main partial's own pattern and level terms are 0 by definition: both are relative to it)
    wo = w.copy(); wo[main] = 0.0
    miss['inharm'] = float((wo * clip(inh)).sum() / max(wo.sum(), 1e-12)) if wo.sum() > 0 else 0.0
    miss['levels'] = float((wo * clip(lv)).sum() / max(wo.sum(), 1e-12)) if wo.sum() > 0 else 0.0
    miss['decay'] = float((w * clip(dc)).sum())
    out['partials'] = rows
    ok = [q for q in rows if 'cents' in q]
    out['decay_log2_median'] = round(float(np.median([np.log2(q['cand_t60'] / q['ref_t60']) for q in ok])), 3) if ok else None
    br = stiff_b([q['f'] for q in rows])
    bc = stiff_b([q['cand_f'] for q in ok])
    out['inharmonicity_B'] = {'ref': br[1] if br else None, 'cand': bc[1] if bc else None}

    # spectral envelope over time
    edges = time_edges(secs)
    gr, gc = ma.band_energy_grid(r, sr, edges), ma.band_energy_grid(c, sr, edges)
    st = spectime(gr, gc, lev)
    st_old = spectime(gr, gc, lev, rule='190ni')
    miss['spectime'] = st['miss']
    if st['db'] is not None:
        out['spectime_db'] = st['db']
    out['spectime_190ni'] = {'db': st_old['db'], 'miss': round(st_old['miss'], 4)}

    # attack
    miss['attack'], out['attack'] = attack_part(r, c, sr)

    sub = {k2: round(10.0 * (1.0 - min(v, 1.0)), 2) for k2, v in miss.items()}
    tw = sum(WEIGHT.values())
    agg = np.sqrt(sum(WEIGHT[k2] * min(v, 1.0) ** 2 for k2, v in miss.items()) / tw)
    out['subscores'] = sub
    out['timbre_score'] = round(float(10.0 * (1.0 - agg)), 2)
    # (the same score with round 190ni's spectro-temporal rule, for comparison with its reports)
    m_old = dict(miss, spectime=out['spectime_190ni']['miss'])
    out['timbre_score_190ni'] = round(float(10.0 * (1.0 - np.sqrt(sum(WEIGHT[k2] * min(v, 1.0) ** 2 for k2, v in m_old.items()) / tw))), 2)
    if wave:
        out['wave'] = wave_metrics(r, c, sr)
    return out


def wave_metrics(r, c, sr):
    """LYNX's waveform-side measures (scripts/flref_compare.py, scripts/fxparity.py), read-only, when present"""
    scripts = os.path.normpath(os.path.join(HERE, '..', '..', 'scripts'))
    try:
        sys.path.insert(0, scripts)
        import flref_compare as fc
        import fxparity
    except Exception:
        return None
    n = min(len(r), len(c))
    fa, la = np.stack([r[:n], r[:n]], 1), np.stack([c[:n], c[:n]], 1)
    w = {'rms_db': round(fc.db(fc.rms(la)) - fc.db(fc.rms(fa)), 2),
         'esr_db': round(fc.db(fc.rms(la - fa)) - fc.db(fc.rms(fa)), 2),
         'spec_db': round(float(fc.spec_distance(fa, la)), 2)}
    w2 = sr // 20
    n2 = n // w2
    if n2 >= 3:
        fw = np.sqrt(np.mean((r[:n2 * w2] ** 2).reshape(n2, -1), 1))
        lw = np.sqrt(np.mean((c[:n2 * w2] ** 2).reshape(n2, -1), 1))
        aud = fw > fw.max() * 0.01
        if aud.sum() >= 3:
            w['env_corr'] = round(float(np.corrcoef(20 * np.log10(fw[aud] + 1e-6), 20 * np.log10(lw[aud] + 1e-6))[0, 1]), 3)
    try:
        w['wave_score'] = fxparity.score(w)
    except Exception:
        pass
    return w


def main():
    a = sys.argv[1:]
    if not a or a[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    if a[0] == 'selftest':
        import unittest
        suite = unittest.defaultTestLoader.discover(HERE, pattern='test_timbre_score.py')
        return 0 if unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful() else 1
    if a[0] == 'pair':
        ref, sr = ma.read_wav(a[1])
        cand, sr2 = ma.read_wav(a[2])
        if sr2 != sr:
            raise SystemExit('sample rates differ: %d vs %d' % (sr, sr2))
        opt = lambda k, d: type(d)(a[a.index(k) + 1]) if k in a else d
        res = score_pair(ref, cand, sr, f0=opt('--f0', 0.0) or None, dur=opt('--dur', 8.0), partials=opt('--partials', 16))
        txt = json.dumps(res, indent=1)
        if '--json' in a:
            open(a[a.index('--json') + 1], 'w').write(txt)
        print(txt)
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
