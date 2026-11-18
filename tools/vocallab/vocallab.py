#!/usr/bin/env python3
# LYNX vocal lab (round 190li): objective scores for monophonic pitch / time / formant edits of a voice.
#
# Every case is a phrase made by vocalsynth.py (known pitch curve, formants and consonants) and an edit; the "ideal" is
# the same phrase rendered from the edited score, so it is what a perfect, transparent editor would output. A render
# (LYNX Newtone at an engine revision, Melodyne 5's export of the same edit, ...) is scored against it:
#
#   pitch      mean |cents| between the render's pitch curve and the ideal's (voiced frames; body / transitions split)
#   gross      % of voiced frames more than 50 cents off (octave errors, wrong notes)
#   formant    rms dB between the render's harmonic amplitudes and the true envelope at those frequencies (mean removed)
#   flux       excess spectral flux at note transitions over the ideal's (dB per 5 ms frame: clicks, warble, smears)
#   hnr        inter-harmonic noise in note bodies, render - ideal (dB; > 0 = noisier, rough / phasey)
#   unvoiced   rms dB of the consonants' third-octave band levels vs the ideal (they should be untouched)
#   smear      burst (t, k) spread 10-90 % energy width, render - ideal (ms) and pre-echo (dB)
#   level      mean |dB| of the notes' level vs the ideal
#
# usage:
#   vocallab.py score <render.wav> <ideal.json> [--src source.wav]          one render, prints the metrics
#   vocallab.py run <material dir> <out dir> --lab <newtonelab> [--engines 0,1] [--melodyne <dir>] [--cases a,b]
#                                                                             the whole matrix -> scores.json + table.md
import json, math, pathlib, subprocess, sys, wave
import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from vocalsynth import envelope_db  # noqa: E402

SR = 48000
HOP = 240


def read_wav(path):
    """Mono float64 at 48 kHz (PCM 16/24/32, float 32; other rates linearly resampled)."""
    b = pathlib.Path(path).read_bytes()
    i, fmt, ch, sr, bits, data = 12, 1, 1, SR, 16, None
    while i + 8 <= len(b):
        cid = b[i:i + 4]; n = int.from_bytes(b[i + 4:i + 8], 'little')
        if cid == b'fmt ':
            fmt = int.from_bytes(b[i + 8:i + 10], 'little'); ch = int.from_bytes(b[i + 10:i + 12], 'little')
            sr = int.from_bytes(b[i + 12:i + 16], 'little'); bits = int.from_bytes(b[i + 22:i + 24], 'little')
            if fmt == 0xFFFE:
                fmt = int.from_bytes(b[i + 32:i + 34], 'little')
        elif cid == b'data':
            data = b[i + 8:i + 8 + n]
        i += 8 + n + (n & 1)
    if fmt == 3 and bits == 32:
        x = np.frombuffer(data, '<f4').astype(np.float64)
    elif fmt == 3 and bits == 64:
        x = np.frombuffer(data, '<f8')
    elif bits == 16:
        x = np.frombuffer(data, '<i2') / 32768.0
    elif bits == 24:
        a = np.frombuffer(data[:len(data) // 3 * 3], np.uint8).reshape(-1, 3).astype(np.int32)
        v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
        x = np.where(v >= 1 << 23, v - (1 << 24), v) / 8388608.0
    else:
        x = np.frombuffer(data, '<i4') / 2147483648.0
    x = x[:len(x) // ch * ch].reshape(-1, ch).mean(1)
    if sr != SR:
        t = np.arange(int(len(x) * SR / sr)) * sr / SR
        x = np.interp(t, np.arange(len(x)), x)
    return x


# ----------------------------------------------------------------------------------------------------------- analysis
def frame(x, centre, n):
    a = centre - n // 2
    if a < 0 or a + n > len(x):
        y = np.zeros(n)
        lo, hi = max(0, a), min(len(x), a + n)
        if hi > lo:
            y[lo - a:hi - a] = x[lo:hi]
        return y
    return x[a:a + n]


def f0_at(x, centre, expect_hz, win=2048, span_cents=250):
    """Pitch of x around `centre` near `expect_hz` (YIN's normalised difference over a lag range of +-span_cents)."""
    w = frame(x, centre, win + 1200)
    if np.sqrt(np.mean(w[:win] ** 2)) < 1e-5:
        return 0.0
    lo = int(SR / (expect_hz * 2 ** (span_cents / 1200))); hi = int(math.ceil(SR / (expect_hz * 2 ** (-span_cents / 1200)))) + 1
    hi = min(hi, 1199); lo = max(lo, 2)
    seg = w[:win]
    # difference function by FFT: d(t) = e0 + e_t - 2 r(t)
    nfft = 1 << int(math.ceil(math.log2(win + hi + 1)))
    A = np.fft.rfft(w[:win + hi], nfft); Bf = np.fft.rfft(seg, nfft)
    r = np.fft.irfft(A * np.conj(Bf), nfft)[:hi + 1]
    c2 = np.cumsum(np.concatenate([[0], w[:win + hi] ** 2]))
    e0 = c2[win] - c2[0]
    et = c2[win:win + hi + 1] - c2[0:hi + 1]
    d = e0 + et - 2 * r
    d[0] = 0
    cm = d[1:] * np.arange(1, hi + 1) / np.maximum(np.cumsum(d[1:]), 1e-18)
    cm = np.concatenate([[1.0], cm])
    k = lo + int(np.argmin(cm[lo:hi]))
    if 1 <= k < hi:
        a, b, g = cm[k - 1], cm[k], cm[k + 1]
        den = a - 2 * b + g
        k = k + (float(np.clip(0.5 * (a - g) / den, -1.0, 1.0)) if abs(den) > 1e-12 else 0)   # (a flat dip: at most a lag)
    return SR / k


def cents(hz):
    return 1200 * np.log2(hz / 440.0) + 6900


def spectrum(x, centre, n=4096):
    w = frame(x, centre, n) * np.hanning(n)
    return np.abs(np.fft.rfft(w)), SR / n


def harmonics_db(x, centre, f0, fmax=5000.0):
    S, df = spectrum(x, centre)
    ks, amps, between = [], [], []
    k = 1
    while k * f0 < fmax:
        fc = k * f0
        a, b = int((fc - f0 / 3) / df), int((fc + f0 / 3) / df) + 1
        if b >= len(S):
            break
        amps.append(20 * np.log10(S[a:b].max() + 1e-12)); ks.append(fc)
        m = int((fc + f0 / 2) / df)
        between.append(20 * np.log10(np.mean(S[max(0, m - 1):m + 2]) + 1e-12))
        k += 1
    return np.array(ks), np.array(amps), np.array(between)


def band_levels(x, a, b):
    """Third-octave band levels (dB) of x[a:b], 500 Hz - 16 kHz."""
    seg = x[max(0, a):max(0, b)]
    if len(seg) < 256:
        return None
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    out = []
    fc = 500.0
    while fc < 16000:
        m = (f >= fc / 2 ** (1 / 6)) & (f < fc * 2 ** (1 / 6))
        out.append(10 * np.log10(S[m].sum() + 1e-20))
        fc *= 2 ** (1 / 3)
    return np.array(out)


def align(render, ideal, max_lag=int(0.12 * SR)):
    """The render shifted (whole samples) to best match the ideal (export offsets, latency): the level envelopes'
    (0.5 ms steps, log) correlation over the first 1.5 s, so a changed pitch does not mislead it."""
    step = 24
    def env(x):
        n = len(x) // step
        return np.log10(np.sqrt((x[:n * step].reshape(n, step) ** 2).mean(1)) + 1e-4)
    n = min(len(ideal), int(1.5 * SR)) // step
    ei, er = env(ideal)[:n], env(render)
    ml = max_lag // step
    er = np.pad(er, (ml, ml + n))
    best, lag = -1e30, 0
    a = ei - ei.mean()
    for l in range(-ml, ml + 1):
        b = er[ml + l:ml + l + n]
        c = float(np.dot(a, b - b.mean()))
        if c > best:
            best, lag = c, l * step
    if lag > 0:
        render = render[lag:]
    elif lag < 0:
        render = np.concatenate([np.zeros(-lag), render])
    if len(render) < len(ideal):
        render = np.pad(render, (0, len(ideal) - len(render)))
    return render[:len(ideal)], lag


# ------------------------------------------------------------------------------------------------------------- scores
ALIGN = False   # (renders are sample-aligned: LYNX by construction, Melodyne's exports checked with an unedited one;
                #  the envelope alignment mis-locked by 0.5-15 ms on pitch-shifted renders. --align turns it on.)


def score(render, ideal, truth):
    hop = truth['hop']
    if ALIGN:
        render, lag = align(render, ideal)
    else:
        lag = 0
        render = np.pad(render, (0, max(0, len(ideal) - len(render))))[:len(ideal)]
    f0c = truth['f0_cents']
    notes = truth['notes']
    peak = max(1e-9, np.sqrt(np.convolve(ideal ** 2, np.ones(960) / 960, 'same')).max())
    bounds = [nt['start'] for nt in notes] + [nt['end'] for nt in notes]
    unv = truth['unvoiced']

    def near_boundary(t, reach=0.06):
        return any(abs(t - b) < reach for b in bounds)

    def in_unvoiced(t):
        return any(u['start'] - 0.01 <= t <= u['end'] + 0.01 for u in unv)

    err_body, err_trans, gross, form_r, form_i, hnr_r, hnr_i = [], [], 0, [], [], [], []
    count = 0
    for fi, c in enumerate(f0c):
        if c is None:
            continue
        pos = fi * hop
        t = pos / SR
        if in_unvoiced(t):
            continue
        if np.sqrt(np.mean(frame(ideal, pos, 960) ** 2)) < 0.03 * peak:
            continue                   # fades: too weak to judge
        expect = 440 * 2 ** ((c / 100 - 69) / 12)
        fi_hz = f0_at(ideal, pos, expect)
        if fi_hz > 0:
            c = cents(fi_hz)        # (the ideal's own measured curve: its jitter and the estimator's bias cancel)
            expect = fi_hz
        fr = f0_at(render, pos, expect)
        if fr <= 0:
            gross += 1; count += 1
            continue
        e = cents(fr) - c
        count += 1
        if abs(e) > 50:
            gross += 1
        (err_trans if near_boundary(t) else err_body).append(abs(e))
        if not near_boundary(t, 0.08) and fi % 4 == 0:
            fm = truth['formants'][fi]; bw = truth['bandwidths'][fi]
            if fm is None:
                continue
            env = list(zip(fm, bw))
            for sig, fo, hn in ((render, form_r, hnr_r), (ideal, form_i, hnr_i)):
                f0m = fr if sig is render else expect
                ks, amps, between = harmonics_db(sig, pos, f0m)
                if len(ks) < 4:
                    continue
                true = envelope_db(ks, env, truth['tilt_hz'])
                # weight by the true level: harmonics far down in a valley are noise either way
                wgt = 10 ** ((true - true.max()) / 20)
                d = (amps - true) - np.average(amps - true, weights=wgt)
                fo.append(math.sqrt(np.average(d ** 2, weights=wgt)))
                hn.append(float(np.average(between - amps, weights=wgt)))
    # spectral flux at transitions (+-60 ms around note starts / ends) and in the bodies
    def flux(sig):
        n = 1024
        S = []
        for pos in range(0, len(sig) - n, HOP):
            S.append(20 * np.log10(np.abs(np.fft.rfft(sig[pos:pos + n] * np.hanning(n)))[2:171] + 1e-7))
        S = np.maximum(np.array(S), 20 * np.log10(peak) - 80)
        return np.concatenate([[0], np.maximum(0, np.diff(S, axis=0)).mean(1)])
    fr_r, fr_i = flux(render), flux(ideal)
    tr = [k for k in range(len(fr_i)) if near_boundary((k * HOP + 512) / SR) and not in_unvoiced((k * HOP + 512) / SR)]
    flux_tr = float(np.mean(fr_r[tr]) - np.mean(fr_i[tr])) if tr else 0.0
    # consonants
    unv_err, smear, pre = [], [], []
    for u in unv:
        a, b = int(u['start'] * SR), int(u['end'] * SR)
        if u['kind'] in ('t', 'k'):
            burst = int((u['start'] + 0.4 * (u['end'] - u['start'])) * SR)
            def spread(sig):
                e = sig[burst - 480:burst + 1440] ** 2
                c = np.cumsum(e) / max(e.sum(), 1e-20)
                return (np.searchsorted(c, 0.9) - np.searchsorted(c, 0.1)) / SR * 1000, max(-60.0, 10 * np.log10(e[:432].sum() / max(e.sum(), 1e-20) + 1e-12))
            sr_, pr_ = spread(render); si_, pi_ = spread(ideal)
            smear.append(sr_ - si_); pre.append(pr_ - pi_)
            a = burst  # the aspiration part for the band check
        lr, li = band_levels(render, a, b), band_levels(ideal, a, b)
        if lr is not None and li is not None:
            m = li > li.max() - 40
            unv_err.append(float(np.sqrt(np.mean((lr[m] - li[m]) ** 2))))
    # note levels
    lev = []
    for nt in notes:
        a, b = int((nt['start'] + 0.05) * SR), int((nt['end'] - 0.05) * SR)
        if b - a > 2400:
            lev.append(10 * np.log10(np.mean(render[a:b] ** 2) + 1e-20) - 10 * np.log10(np.mean(ideal[a:b] ** 2) + 1e-20))
    return {
        'pitch': round(float(np.mean(err_body + err_trans)), 2) if err_body + err_trans else None,
        'pitch_body': round(float(np.mean(err_body)), 2) if err_body else None,
        'pitch_trans': round(float(np.mean(err_trans)), 2) if err_trans else None,
        'gross': round(100.0 * gross / max(1, count), 2),
        'formant': round(float(np.mean(form_r)), 2) if form_r else None,
        'formant_floor': round(float(np.mean(form_i)), 2) if form_i else None,
        'flux': round(flux_tr, 3),
        'hnr': round(float(np.mean(hnr_r) - np.mean(hnr_i)), 2) if hnr_r and hnr_i else None,
        'unvoiced': round(float(np.mean(unv_err)), 2) if unv_err else None,
        'smear_ms': round(float(np.mean(smear)), 2) if smear else None,
        'preecho_db': round(float(np.mean(pre)), 2) if pre else None,
        'level': round(float(np.mean(np.abs(lev))), 2) if lev else None,
        'level_mean': round(float(np.mean(lev)), 2) if lev else None,
        'lag': lag,
    }


# ---------------------------------------------------------------------------------------------------------- the matrix
# LYNX newtonelab arguments per edit and engine ({t4} = the middle of the 4th sung note in the source)
def lab_args(edit, engine, t4, d4=0.0):
    e1 = engine >= 1
    table = {
        'up3': ['shift=all:3'], 'down3': ['shift=all:-3'], 'up7': ['shift=all:7'], 'down7': ['shift=all:-7'],
        'one-up3': [f'shift=@{t4:.3f}:3'],
        'centre50': ['centre=0.5'], 'centre100': ['centre=1'],
        # (engine 0 has no drift / vibrato split: FL's own tool for it is Variation, which takes both)
        'drift0': ['pitchdrift=0'] if e1 else ['variation=0'],
        'correct100': ['centre=1', 'pitchdrift=0'] if e1 else ['centre=1', 'variation=0'],
        'mod0': ['modulation=0'] if e1 else ['variation=0'],
        'mod150': ['modulation=1.5'] if e1 else ['variation=1.5'],
        'formant+3': ['formant=all:3'],
        'gain-6': [f'volume=@{t4:.3f}:-6'],
        'stretch125': [f'lengthen=@{t4:.3f}:{0.25 * d4:.4f}'],
    }
    return table[edit]


METRICS = ['pitch', 'pitch_trans', 'gross', 'formant', 'flux', 'hnr', 'unvoiced', 'smear_ms', 'level']
# lower is better for every metric except none; hnr / smear / flux: closer to 0 (or below) is better
def worse(a, b, m, tol):
    """True if value a is worse than b (beyond tol) for metric m."""
    if a is None or b is None:
        return False
    if m in ('hnr', 'flux', 'smear_ms'):
        return max(a, 0) > max(b, 0) + tol
    return a > b + tol


TOL = {'pitch': 0.3, 'pitch_trans': 0.5, 'gross': 0.3, 'formant': 0.1, 'flux': 0.03, 'hnr': 0.3, 'unvoiced': 0.2,
       'smear_ms': 0.2, 'level': 0.1}


def run(argv):
    mat = pathlib.Path(argv[0]); out = pathlib.Path(argv[1]); out.mkdir(parents=True, exist_ok=True)
    lab = argv[argv.index('--lab') + 1]
    engines = [int(e) for e in (argv[argv.index('--engines') + 1] if '--engines' in argv else '0,1').split(',')]
    mel = pathlib.Path(argv[argv.index('--melodyne') + 1]) if '--melodyne' in argv else None
    only = set(argv[argv.index('--cases') + 1].split(',')) if '--cases' in argv else None
    extra = argv[argv.index('--extra') + 1].split() if '--extra' in argv else []
    externs = [argv[i + 1] for i, a in enumerate(argv) if a == '--extern']
    results = {}
    for ideal_json in sorted(mat.glob('*.ideal.*.json')):
        phrase, _, edit = ideal_json.name[:-5].split('.', 2)
        case = f'{phrase}/{edit}'
        if only and edit not in only and case not in only:
            continue
        truth = json.loads(ideal_json.read_text())
        src_truth = json.loads((mat / f'{phrase}.json').read_text())
        n4 = src_truth['notes'][3]
        t4 = 0.5 * (n4['start'] + n4['end']); d4 = n4['end'] - n4['start']
        ideal = read_wav(mat / f'{phrase}.ideal.{edit}.wav')
        res = {}
        for eng in engines:
            o = out / f'{phrase}.{edit}.e{eng}.wav'
            cmd = [lab, str(mat / f'{phrase}.wav'), str(o), f'engine={eng}'] + extra + lab_args(edit, eng, t4, d4)
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
            res[f'lynx-e{eng}'] = score(read_wav(o), ideal, truth)
        for ex in externs:
            # an external engine for the uniform shifts: NAME:command with {in} {out} {semis}
            name, cmd = ex.split(':', 1)
            semis = {'up3': 3, 'down3': -3, 'up7': 7, 'down7': -7}.get(edit)
            if semis is None:
                continue
            o = out / f'{phrase}.{edit}.{name}.wav'
            subprocess.run(cmd.format(**{'in': str(mat / f'{phrase}.wav'), 'out': str(o), 'semis': semis}), shell=True, check=True, stdout=subprocess.DEVNULL)
            res[name] = score(read_wav(o), ideal, truth)
        if mel is not None:
            m = mel / f'{phrase}.{edit}.wav'
            if m.exists():
                res['melodyne'] = score(read_wav(m), ideal, truth)
        res['ideal'] = score(ideal, ideal, truth)
        results[case] = res
        print(case, ' | '.join(f"{k}: p {v['pitch']} f {v['formant']} fl {v['flux']} h {v['hnr']} u {v['unvoiced']} l {v['level']}" for k, v in res.items()), flush=True)
    (out / 'scores.json').write_text(json.dumps(results, indent=1))
    (out / 'table.md').write_text(table(results))
    print(table(results))


def table(results):
    cols = sorted({k for r in results.values() for k in r})
    lines = ['| case | metric | ' + ' | '.join(cols) + ' |', '|' + '---|' * (len(cols) + 2)]
    for case, r in results.items():
        for m in METRICS:
            vals = [r.get(c, {}).get(m) for c in cols]
            if all(v is None for v in vals):
                continue
            lines.append(f'| {case} | {m} | ' + ' | '.join('' if v is None else f'{v:g}' for v in vals) + ' |')
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    if '--align' in sys.argv:
        ALIGN = True
        sys.argv.remove('--align')
    if len(sys.argv) > 1 and sys.argv[1] == 'run':
        run(sys.argv[2:])
    elif len(sys.argv) > 3 and sys.argv[1] == 'score':
        t = json.loads(pathlib.Path(sys.argv[3]).read_text())
        ideal = read_wav(sys.argv[3][:-5] + '.wav')
        print(json.dumps(score(read_wav(sys.argv[2]), ideal, t), indent=1))
    else:
        print(__doc__ if __doc__ else 'see the header')
