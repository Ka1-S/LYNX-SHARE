#!/usr/bin/env python3
"""flref_compare.py — compares FL Studio reference renders with LYNX renders of the same projects.

Usage:
  flref_compare.py pair <fl.wav> <lynx.raw|lynx.wav>          one comparison, printed as JSON
  flref_compare.py batch <fl-dir> <lynx-dir> [report.md]     every <fl-dir>/<g>/<n>.wav with <lynx-dir>/<g>/<n>.raw
  flref_compare.py culprit <fl.wav> <lynx.raw> <stem.raw>...  ranks LYNX channel stems by how well they explain the
                                                               residual (LYNX - FL) after alignment

Metrics (after the alignment search, over the overlapping part):
  lag       samples LYNX is late (+) / early (-) relative to FL (cross-correlation of the mono sums; search window
            +-LAG_WINDOW_S seconds, never fewer than 4096 samples: +-4096 at 44.1 / 48 kHz, +-8192 at 96 kHz)
  peak_db   20log10(peak LYNX / peak FL)
  rms_db    20log10(rms LYNX / rms FL)
  esr_db    error-to-signal ratio 20log10(rms(L-F)/rms(F)); <= -40 dB is close, >= 0 dB is unrelated
  gesr_db   the same after the best single gain (a pure level error has a low gesr_db)
  worst_s   the 1-second window with the largest error (seconds into FL's render) and its local esr
  match40/60  % of the audible 50 ms windows whose local ESR is <= -40 / -60 dB (int16 quantization allows ~-80)
  env_corr  correlation of the 50 ms level envelopes (dB, mono) over FL's audible windows: phase-blind timing/level
  spec_db   mean |level difference| over third-octave bands 31 Hz…16 kHz of the long-term spectra (first 5 minutes)
            (env_corr and spec_db stay meaningful for whole songs, where any phase drift makes esr_db ~0 dB)
  bands     level difference (dB) in low (<200 Hz), mid (200 Hz–4 kHz), high (>4 kHz) bands
LYNX raw files are renderwav's LYNX_RAW output (float32, left block then right block); FL wavs are 16-bit PCM.
"""
import json
import os
import sys
import wave

import numpy as np

SR = 44100
# Alignment search window in time (round 190lg): 4096 samples at 48 kHz (85.3 ms) at every rate. A fixed 4096-sample
# window was only 42.7 ms at 96 kHz, so a reference with ~63 ms of latency (Pro-L 2 Dense / Loud / Safe) failed alignment
# there. Never narrower than the old 4096 samples, so 44.1 / 48 kHz results are bit-identical to the old scorer.
LAG_WINDOW_S = 4096 / 48000.0
LAG_MIN = 4096


def lag_window(sr=None):
    sr = SR if sr is None else sr
    return max(LAG_MIN, int(np.ceil(LAG_WINDOW_S * sr - 1e-9)))


def read_fl(path):
    with wave.open(path, 'rb') as w:
        ch, sw, n = w.getnchannels(), w.getsampwidth(), w.getnframes()
        b = w.readframes(n)
    if sw == 2:
        x = np.frombuffer(b, '<i2').astype(np.float64) / 32767.0
    elif sw == 3:
        a = np.frombuffer(b, np.uint8).reshape(-1, 3)
        x = ((a[:, 0].astype(np.int32) | (a[:, 1].astype(np.int32) << 8) | (a[:, 2].astype(np.int32) << 16)) << 8 >> 8) / 8388607.0
    elif sw == 4:
        x = np.frombuffer(b, '<f4').astype(np.float64)
    else:
        raise ValueError('sample width %d' % sw)
    x = x.reshape(-1, ch)
    if ch == 1:
        x = np.repeat(x, 2, axis=1)
    return x[:, :2]


def read_lynx(path):
    if path.endswith('.wav'):
        return read_fl(path)
    a = np.fromfile(path, '<f4').astype(np.float64)
    n = len(a) // 2
    return np.stack([a[:n], a[n:2 * n]], axis=1)


def db(x, floor=1e-12):
    return 20.0 * np.log10(max(x, floor))


def rms(x):
    return float(np.sqrt(np.mean(x * x))) if len(x) else 0.0


def find_lag(f, l, maxlag=None, span=None):
    """Lag (LYNX later = +) maximizing the cross-correlation of the mono sums over the first `span` samples (default
    1 764 000 = 40 s at 44.1 kHz at every rate, as before) within +-maxlag samples (default lag_window(): +-85 ms at the current SR, at least 4096 samples)."""
    if maxlag is None:
        maxlag = lag_window()
    if span is None:
        span = 40 * 44100
    fm = f[:span].sum(axis=1)
    lm = l[:span + maxlag].sum(axis=1)
    if rms(fm) < 1e-7 or rms(lm) < 1e-7:
        return 0, 0.0
    n = 1
    while n < len(fm) + len(lm):
        n *= 2
    c = np.fft.irfft(np.fft.rfft(lm, n) * np.conj(np.fft.rfft(fm, n)), n)
    lags = np.r_[np.arange(0, maxlag + 1), np.arange(-maxlag, 0)]
    vals = np.r_[c[:maxlag + 1], c[n - maxlag:]]
    i = int(np.argmax(vals))
    norm = np.sqrt(np.sum(fm * fm) * np.sum(lm * lm))
    return int(lags[i]), float(vals[i] / norm) if norm > 0 else 0.0


def align(f, l, lag):
    if lag >= 0:
        l = l[lag:]
    else:
        f = f[-lag:]
    n = min(len(f), len(l))
    return f[:n], l[:n]


def band_levels(x):
    m = x.sum(axis=1)
    if len(m) > 60 * SR:  # spectrum of the first minute is enough
        m = m[:60 * SR]
    s = np.abs(np.fft.rfft(m)) ** 2
    fr = np.fft.rfftfreq(len(m), 1.0 / SR)
    return [float(np.sum(s[(fr >= lo) & (fr < hi)])) for lo, hi in ((20, 200), (200, 4000), (4000, 22050))]


def third_octaves(x):
    m = x.sum(axis=1)[:300 * SR]
    nf = 1 << 15
    nseg = max(1, len(m) // nf)
    s = np.zeros(nf // 2 + 1)
    win = np.hanning(nf)
    for i in range(nseg):
        seg = m[i * nf:(i + 1) * nf]
        if len(seg) < nf:
            seg = np.pad(seg, (0, nf - len(seg)))
        s += np.abs(np.fft.rfft(seg * win)) ** 2
    fr = np.fft.rfftfreq(nf, 1.0 / SR)
    centres = 1000.0 * 2.0 ** (np.arange(-15, 13) / 3.0)  # 31 Hz … 16 kHz
    return np.array([np.sum(s[(fr >= c * 2 ** (-1 / 6)) & (fr < c * 2 ** (1 / 6))]) for c in centres])


def spec_distance(fa, la):
    bf, bl = third_octaves(fa), third_octaves(la)
    ref = bf.max()
    use = bf > ref * 1e-6  # bands within 60 dB of FL's loudest
    if not use.any():
        return 0.0
    d = 10 * np.log10(np.maximum(bl[use], 1e-30) / bf[use])
    return float(np.mean(np.abs(np.clip(d, -60, 60))))


def apply_mask(fa, la, lag, spans):
    """Round 190mm: drop the reference's masked spans (demo-mode mutes; [start, end) in reference samples) from both
    aligned signals. fa[k] is reference sample k + max(0, -lag). The kept parts are joined; the error, level, spectrum
    and envelope metrics then see only audio both sides really played."""
    keep = np.ones(len(fa), bool)
    off = max(0, -lag)
    for s0, s1 in spans:
        a, b = max(0, int(s0) - off), min(len(fa), int(s1) - off)
        if b > a:
            keep[a:b] = False
    return fa[keep], la[keep], int(len(keep) - keep.sum())


def compare(flpath, lypath, mask=None):
    """`mask` (round 190mm): optional [start, end) spans in reference samples left out of every metric (the Waves
    demo mode's periodic mutes, detected in the capture and stored next to the reference, see fxparity.py). None =
    the unmasked comparison, bit-identical to before."""
    f = read_fl(flpath)
    l = read_lynx(lypath)
    r = {'fl_s': round(len(f) / SR, 2), 'lynx_s': round(len(l) / SR, 2),
         'fl_peak_db': round(db(float(np.max(np.abs(f))) if len(f) else 0), 2),
         'lynx_peak_db': round(db(float(np.max(np.abs(l))) if len(l) else 0), 2)}
    if not np.isfinite(l).all():
        r['nan'] = True
        l = np.nan_to_num(l)
    fl_silent = r['fl_peak_db'] < -90
    ly_silent = r['lynx_peak_db'] < -90
    r['silent'] = 'both' if fl_silent and ly_silent else 'fl' if fl_silent else 'lynx' if ly_silent else ''
    lag, corr = find_lag(f, l)
    r['lag'] = lag
    r['xcorr'] = round(corr, 4)
    fa, la = align(f, l, lag)
    if mask:
        fa, la, r['masked'] = apply_mask(fa, la, lag, mask)
    pf, pl = float(np.max(np.abs(fa))) if len(fa) else 0.0, float(np.max(np.abs(la))) if len(la) else 0.0
    rf, rl = rms(fa), rms(la)
    e = la - fa
    r['peak_db'] = round(db(pl) - db(pf), 2)
    r['rms_db'] = round(db(rl) - db(rf), 2)
    r['esr_db'] = round(db(rms(e)) - db(rf), 2) if rf > 0 else (0.0 if rms(e) == 0 else 99.0)
    g = float(np.sum(la * fa) / np.sum(la * la)) if np.sum(la * la) > 0 else 0.0
    r['gain'] = round(g, 4)
    r['gesr_db'] = round(db(rms(g * la - fa)) - db(rf), 2) if rf > 0 else r['esr_db']
    # worst 1-second window (relative to the overall FL level, so quiet passages don't dominate)
    w = SR
    nw = len(fa) // w
    if nw > 0:
        ew = np.sqrt(np.mean((e[:nw * w] ** 2).reshape(nw, -1), axis=1))
        i = int(np.argmax(ew))
        fw = rms(fa[i * w:(i + 1) * w])
        r['worst_s'] = round((i * w + max(0, -lag)) / SR, 1)
        r['worst_esr_db'] = round(db(float(ew[i])) - db(fw), 2) if fw > 0 else 99.0
        r['worst_err_db'] = round(db(float(ew[i])) - db(rf), 2) if rf > 0 else 99.0
    # share of the audible 50 ms windows (within 40 dB of FL's loudest) whose local ESR is <= -40 dB / -60 dB
    w2 = SR // 20
    n2 = len(fa) // w2
    if n2 > 0:
        fw2 = np.sqrt(np.mean((fa[:n2 * w2] ** 2).reshape(n2, -1), axis=1))
        ew2 = np.sqrt(np.mean((e[:n2 * w2] ** 2).reshape(n2, -1), axis=1))
        aud = fw2 > fw2.max() * 0.01
        if aud.any():
            loc = ew2[aud] / fw2[aud]
            r['match40'] = round(100.0 * float(np.mean(loc <= 0.01)), 1)
            r['match60'] = round(100.0 * float(np.mean(loc <= 0.001)), 1)
    if n2 > 0 and aud.any():
        ef = 20 * np.log10(np.maximum(fw2, 1e-6))
        el = 20 * np.log10(np.maximum(np.sqrt(np.mean((la[:n2 * w2] ** 2).reshape(n2, -1), axis=1)), 1e-6))
        if np.std(ef[aud]) > 0 and np.std(el[aud]) > 0:
            r['env_corr'] = round(float(np.corrcoef(ef[aud], el[aud])[0, 1]), 3)
    r['spec_db'] = round(spec_distance(fa, la), 2)
    bf, bl = band_levels(fa), band_levels(la)
    r['bands'] = [round(10 * np.log10(max(b, 1e-20) / max(a, 1e-20)), 1) for a, b in zip(bf, bl)]
    return r


def culprit(flpath, lypath, stems):
    """Ranks stems by the share of the residual energy envelope they explain (50 ms windows)."""
    f = read_fl(flpath)
    l = read_lynx(lypath)
    lag, _ = find_lag(f, l)
    fa, la = align(f, l, lag)
    e = la - fa
    w = SR // 20
    n = len(e) // w

    def env(x):
        x = x[:n * w]
        if len(x) < n * w:
            x = np.pad(x, ((0, n * w - len(x)), (0, 0)))
        return np.sqrt(np.mean((x ** 2).reshape(n, -1), axis=1))

    ee = env(e)
    out = []
    for s in stems:
        x = read_lynx(s)
        x = x[lag:] if lag >= 0 else np.pad(x, ((-lag, 0), (0, 0)))
        se = env(x)
        c = float(np.corrcoef(ee, se)[0, 1]) if np.std(ee) > 0 and np.std(se) > 0 else 0.0
        # energy of the residual inside the windows where this stem is active
        act = se > 0.05 * (se.max() if se.max() > 0 else 1)
        share = float(np.sum(ee[act] ** 2) / max(np.sum(ee ** 2), 1e-30))
        out.append({'stem': os.path.basename(s), 'env_corr': round(c, 3), 'resid_share': round(share, 3),
                    'stem_rms_db': round(db(rms(x)), 1)})
    out.sort(key=lambda d: -d['env_corr'])
    return out


def score(r):
    """Sort key: worst first (silence mismatch, then error-to-signal ratio)."""
    if r.get('missing'):
        return 1000.0
    if r.get('silent') in ('fl', 'lynx'):
        return 500.0
    if r.get('silent') == 'both':
        return -1000.0
    return r.get('esr_db', 0.0)


def batch(fldir, lydir, report):
    rows = []
    for g in sorted(os.listdir(fldir)):
        gd = os.path.join(fldir, g)
        if not os.path.isdir(gd):
            continue
        for fn in sorted(os.listdir(gd)):
            if not fn.endswith('.wav'):
                continue
            n = fn[:-4]
            ly = os.path.join(lydir, g, n + '.raw')
            r = {'group': g, 'name': n}
            if not os.path.exists(ly):
                r['missing'] = True
            else:
                try:
                    r.update(compare(os.path.join(gd, fn), ly))
                except Exception as ex:  # keep the batch going
                    r['error'] = str(ex)
                    r['missing'] = True
            info = os.path.join(lydir, g, n + '.log')
            if os.path.exists(info):
                r['channels'] = [ln.strip() for ln in open(info, errors='replace') if '->' in ln]
            rows.append(r)
            print(json.dumps(r), flush=True)
    with open(os.path.join(lydir, 'compare.jsonl'), 'w') as fo:
        for r in rows:
            fo.write(json.dumps(r) + '\n')
    if report:
        write_report(rows, report)


def write_report(rows, report):
    """Two tables: single-plugin render tests ranked by esr_db, everything else ranked by spec_db (phase-blind)."""
    def nm(r):
        return r['group'].replace('Demo_projects-', '').replace('System-Render_tests', 'RT') + '/' + r['name']

    done = [r for r in rows if not r.get('missing')]
    rt = [r for r in done if r['group'] == 'System-Render_tests' and r.get('silent') != 'both']
    songs = [r for r in done if r['group'] != 'System-Render_tests' and r.get('silent') != 'both']

    def bucket(rs, lo, hi):
        return sum(1 for r in rs if r.get('silent') == '' and lo <= r['esr_db'] < hi)

    lines = ['# FL reference renders vs LYNX — scoreboard', '',
             '%d projects; %d compared, %d silent in both (empty templates etc.), %d missing.' % (
                 len(rows), len(rt) + len(songs), sum(1 for r in done if r.get('silent') == 'both'),
                 sum(1 for r in rows if r.get('missing'))), '',
             '| ESR bucket | render tests | other projects |', '|---|---|---|']
    for label, lo, hi in (('<= -60 dB (exact to int16)', -1e9, -60), ('-60 … -40 dB', -60, -40), ('-40 … -20 dB', -40, -20),
                          ('-20 … -6 dB', -20, -6), ('> -6 dB', -6, 1e9)):
        lines.append('| %s | %d | %d |' % (label, bucket(rt, lo, hi), bucket(songs, lo, hi)))
    lines.append('| silent in one only | %d | %d |' % (sum(1 for r in rt if r.get('silent')), sum(1 for r in songs if r.get('silent'))))
    lines += ['', '## Render tests (single plugin), worst first', '',
              '| # | project | lag | peak dB | RMS dB | ESR dB | gain-ESR dB | match -40/-60 % | worst s (ESR) | bands L/M/H dB | note |',
              '|---|---|---|---|---|---|---|---|---|---|---|']
    for i, r in enumerate(sorted(rt, key=score, reverse=True), 1):
        note = ('silent: ' + r['silent']) if r.get('silent') else ''
        lines.append('| %d | %s | %d | %+.2f | %+.2f | %.1f | %.1f | %s/%s | %s (%s) | %s | %s |' % (
            i, nm(r), r['lag'], r['peak_db'], r['rms_db'], r['esr_db'], r['gesr_db'], r.get('match40', ''),
            r.get('match60', ''), r.get('worst_s', ''), r.get('worst_esr_db', ''),
            '/'.join('%+.1f' % b for b in r['bands']), note))

    def skey(r):
        return 1000.0 if r.get('silent') else r.get('spec_db', 0.0)

    lines += ['', '## Songs, demos and templates, worst first (by third-octave spectrum distance)', '',
              '| # | project | FL s | LYNX s | RMS dB | spec dB | env corr | ESR dB | lag | bands L/M/H dB | note |',
              '|---|---|---|---|---|---|---|---|---|---|---|']
    for i, r in enumerate(sorted(songs, key=skey, reverse=True), 1):
        note = ('silent: ' + r['silent']) if r.get('silent') else ''
        lines.append('| %d | %s | %.1f | %.1f | %+.2f | %.2f | %s | %.1f | %d | %s | %s |' % (
            i, nm(r), r['fl_s'], r['lynx_s'], r['rms_db'], r.get('spec_db', 0.0), r.get('env_corr', ''), r['esr_db'],
            r['lag'], '/'.join('%+.1f' % b for b in r['bands']), note))
    for r in rows:
        if r.get('missing'):
            lines.append('- missing: %s %s' % (nm(r), r.get('error', '')))
    open(report, 'w').write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'pair':
        print(json.dumps(compare(sys.argv[2], sys.argv[3]), indent=1))
    elif cmd == 'batch':
        batch(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
    elif cmd == 'culprit':
        for d in culprit(sys.argv[2], sys.argv[3], sys.argv[4:]):
            print(json.dumps(d))
    else:
        sys.exit(__doc__)
