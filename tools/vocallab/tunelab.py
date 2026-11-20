#!/usr/bin/env python3
# LYNX vocal lab (round 190li): LYNX Tune (the real-time corrector) at moderate retune speeds, scored for naturalness.
#
# Each phrase of vocalsynth.py runs through tools/lynxtunelab (chromatic scale) at a few retune times and is scored
# (vocallab.score) against two ideals:
#   centre   the parent's centre100 ideal: every note's detune removed, drift / vibrato / scoops / glides kept (what a
#            transparent, slow corrector converges to)
#   model    the same phrase synthesised from the corrector's own law applied to the TRUE pitch curve: target = the
#            nearest semitone (hysteresis 0.03 st), correction = a one-pole at the retune time, reset at each phrase
#            (> 60 ms unvoiced). Anything the render does beyond it is an artefact of detection / shifting.
# plus its own measures of the render:
#   vib      vibrato depth kept (output / source, on the notes with vibrato; the model's own value next to it)
#   steps    pitch steps: frames where the render's pitch curve jumps > 12 c in 5 ms more than the model's does
#   jitter   frame-to-frame wobble of the render's pitch around the model's (rms cents, target flips left out)
#   click    voiced / unvoiced switches: the worst 1 ms high-band (> 5 kHz) level within 10 ms of each switch,
#            render − source (dB, > 0 = a click; click_mean = the mean over the switches)
#   cpu      instructions (or ns) per sample of the offline run
#
# usage: tunelab.py <material dir> <out dir> --lab <lynxtunelab> [--retunes 0,20,50,100] [--extra "rev=1 ..."]
#                   [--tag name] [--phrases f-phrase,...]
import inspect, json, math, pathlib, subprocess, sys
import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import vocalsynth as vs  # noqa: E402
import vocallab as vl  # noqa: E402

SR = vl.SR

# vocalsynth.render with a hook on the pitch curve (cents per sample, NaN = unvoiced) before the voice is rendered
_src = inspect.getsource(vs.render).replace('def render(phrase, want_truth=False):', 'def render_hooked(phrase, want_truth=False, _hook=None):')
_src = _src.replace('    voiced = ~np.isnan(cents)\n', '    cents = _hook(cents) if _hook is not None else cents\n    voiced = ~np.isnan(cents)\n', 1)
assert '_hook(cents)' in _src
exec(compile(_src, 'render_hooked', 'exec'), vs.__dict__)


def model_curve(cents, retune_ms, hyst=0.03, hop=139):
    """The corrector's law on a true pitch curve (cents per sample): the correction re-decided every `hop` samples
    (LYNX's detector hop at 48 kHz), followed by a one-pole of the retune time."""
    out = cents.copy()
    n = len(cents)
    tau = retune_ms * 0.001
    a = 1.0 - math.exp(-1.0 / (tau * SR)) if tau > 0 else 1.0
    shift = 0.0; wanted = 0.0; target = -1; unv = 1e9
    for i0 in range(0, n, hop):
        c = cents[min(i0 + hop // 2, n - 1)]
        if not np.isnan(c):
            sung = c / 100.0
            new_phrase = unv > 0.06
            if new_phrase:
                target = -1; shift = 0.0
            if target >= 0 and abs(sung - target) < 0.5 + hyst:
                t = target
            else:
                t = int(round(sung))
            target = t
            wanted = t - sung
            if new_phrase and tau <= 0:
                shift = wanted
            unv = 0.0
        else:
            unv += hop / SR
        seg = slice(i0, min(n, i0 + hop))
        if a >= 1.0:
            shift = wanted
            s = np.full(seg.stop - seg.start, shift)
        else:
            k = np.arange(1, seg.stop - seg.start + 1)
            s = wanted + (shift - wanted) * (1 - a) ** k
            shift = s[-1]
        v = ~np.isnan(cents[seg])
        out[seg] = np.where(v, cents[seg] + 100.0 * s, np.nan)
    return out


def model_ideal(phrase, retune_ms, scale, cache):
    w, j = cache / f'model{retune_ms:g}.{phrase}.wav', cache / f'model{retune_ms:g}.{phrase}.json'
    if not w.exists():
        y, t = vs.render_hooked(vs.PHRASES[phrase].copy(), True, lambda c: model_curve(c, retune_ms))
        t['scale'] = scale
        vs.write_wav(w, y * scale)
        j.write_text(json.dumps(t))
    return vl.read_wav(w), json.loads(j.read_text())


def pitch_curve(x, truth, frames):
    """The render's pitch (cents) at truth frames, measured near the truth's expectation (None = not found)."""
    out = []
    for fi in frames:
        c = truth['f0_cents'][fi]
        if c is None:
            out.append(np.nan); continue
        hz = vl.f0_at(x, fi * truth['hop'], 440 * 2 ** ((c / 100 - 69) / 12), span_cents=150)
        v = vl.cents(hz) if hz > 0 else np.nan
        # (f0_at's parabolic step is unbounded on a flat dip: a reading outside the searched span is no reading)
        out.append(v if abs(v - c) <= 160 else np.nan)
    return np.array(out)


def vib_depth(curve, rate, hop_s):
    """Peak depth (cents) of the vibrato in a pitch curve: a least-squares sine (+ line) at the note's own rate."""
    m = ~np.isnan(curve)
    if m.sum() < 20:
        return None
    t = np.arange(len(curve))[m] * hop_s
    A = np.stack([np.sin(2 * np.pi * rate * t), np.cos(2 * np.pi * rate * t), np.ones_like(t), t, t * t], 1)
    coef, *_ = np.linalg.lstsq(A, curve[m], rcond=None)
    return float(math.hypot(coef[0], coef[1]))


def own_measures(render, src, model, src_truth, model_truth):
    hop = src_truth['hop']; hop_s = hop / SR
    res = {}
    # vibrato kept
    ratios, mratios = [], []
    for nt, pn in zip(src_truth['notes'], vs.PHRASES[src_truth['_phrase']].notes):
        if pn.vib <= 0:
            continue
        a = nt['start'] + pn.vib_delay + 0.15; b = nt['end'] - 0.08
        frames = list(range(int(a * SR / hop), int(b * SR / hop)))
        if len(frames) < 30:
            continue
        ds = vib_depth(pitch_curve(src, src_truth, frames), pn.vib_rate, hop_s)
        dr = vib_depth(pitch_curve(render, src_truth, frames), pn.vib_rate, hop_s)
        dm = vib_depth(pitch_curve(model, model_truth, frames), pn.vib_rate, hop_s)
        if ds and dr is not None:
            ratios.append(dr / ds)
            mratios.append(dm / ds if dm is not None else np.nan)
    res['vib'] = round(float(np.mean(ratios)), 3) if ratios else None
    res['vib_model'] = round(float(np.nanmean(mratios)), 3) if mratios else None
    # pitch steps vs the model
    frames = [i for i, c in enumerate(model_truth['f0_cents']) if c is not None]
    cr = pitch_curve(render, model_truth, frames); cm = pitch_curve(model, model_truth, frames)
    fr = np.array(frames)
    ok = (np.diff(fr) == 1) & ~np.isnan(cr[1:]) & ~np.isnan(cr[:-1]) & ~np.isnan(cm[1:]) & ~np.isnan(cm[:-1])
    jr = np.abs(np.diff(cr))[ok]; jm = np.abs(np.diff(cm))[ok]
    res['steps'] = int(np.sum((jr > 12) & (jr > jm + 8)))
    # frame-to-frame wobble of the render's pitch around the model's (rms cents; frames where either changes note or
    # reads > 40 c apart are left out: those are target flips, counted by `steps`)
    dd = np.diff(cr - cm)[ok]
    near = (np.abs((cr - cm)[1:][ok]) < 40) & (np.abs((cr - cm)[:-1][ok]) < 40)
    res['jitter'] = round(float(np.sqrt(np.mean(dd[near] ** 2))), 2) if near.any() else None
    # clicks at voiced / unvoiced switches (high band, 1 ms frames)
    def hp(x):
        X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR)
        X[f < 5000] = 0
        return np.fft.irfft(X, len(x))
    hr, hs = hp(render), hp(src)
    def lev(h, c):
        seg = h[max(0, c - 480):c + 480]
        n = len(seg) // 48
        return 10 * np.log10((seg[:n * 48].reshape(n, 48) ** 2).mean(1).max() + 1e-14)
    sw = []
    for u in src_truth['unvoiced']:
        for t in (u['start'], u['end']):
            sw.append(int(t * SR))
    for nt in src_truth['notes']:
        sw.append(int(nt['start'] * SR)); sw.append(int(nt['end'] * SR))
    clicks = [lev(hr, c) - lev(hs, c) for c in sorted(set(sw))]
    res['click'] = round(float(max(clicks)), 2) if clicks else None
    res['click_mean'] = round(float(np.mean(clicks)), 2) if clicks else None
    return res


METRICS = ['pitch', 'pitch_trans', 'gross', 'formant', 'flux', 'hnr', 'unvoiced', 'smear_ms', 'level']
OWN = ['vib', 'steps', 'jitter', 'click', 'click_mean']


def main(argv):
    mat = pathlib.Path(argv[0]); out = pathlib.Path(argv[1]); out.mkdir(parents=True, exist_ok=True)
    lab = argv[argv.index('--lab') + 1]
    retunes = [float(r) for r in (argv[argv.index('--retunes') + 1] if '--retunes' in argv else '0,20,50,100').split(',')]
    extra = argv[argv.index('--extra') + 1].split() if '--extra' in argv else []
    tag = argv[argv.index('--tag') + 1] if '--tag' in argv else 'run'
    phrases = (argv[argv.index('--phrases') + 1] if '--phrases' in argv else 'f-phrase,m-phrase,i-legato').split(',')
    cache = out / 'ideals'; cache.mkdir(exist_ok=True)
    results = {}
    for ph in phrases:
        src_truth = json.loads((mat / f'{ph}.json').read_text()); src_truth['_phrase'] = ph
        src = vl.read_wav(mat / f'{ph}.wav')
        centre = vl.read_wav(mat / f'{ph}.ideal.centre100.wav')
        centre_truth = json.loads((mat / f'{ph}.ideal.centre100.json').read_text())
        for rt in retunes:
            model, model_truth = model_ideal(ph, rt, src_truth['scale'], cache)
            o = out / f'{ph}.r{rt:g}.{tag}.wav'
            cmd = [lab, str(mat / f'{ph}.wav'), str(o), f'retune={rt:g}', 'bench=2'] + extra
            info = json.loads(subprocess.run(cmd, check=True, capture_output=True, text=True).stdout.strip().splitlines()[-1])
            render = vl.read_wav(o)
            r = {'centre': vl.score(render, centre, centre_truth), 'model': vl.score(render, model, model_truth)}
            r['own'] = own_measures(render, src, model, src_truth, model_truth)
            r['cpu'] = {'ins': round(info['instructions_per_sample'], 1), 'ns': round(info['ns_per_sample'], 1)}
            results[f'{ph}/r{rt:g}'] = r
            print(f'{ph} r{rt:g}', 'model:', {k: r['model'][k] for k in METRICS}, 'centre pitch', r['centre']['pitch'],
                  'own:', r['own'], r['cpu'], flush=True)
    (out / f'scores.{tag}.json').write_text(json.dumps(results, indent=1))


def compare(argv):
    """tunelab.py compare <out dir> <tag A> <tag B>: per case, every metric A -> B, worse ones marked."""
    out = pathlib.Path(argv[0])
    A = json.loads((out / f'scores.{argv[1]}.json').read_text()); B = json.loads((out / f'scores.{argv[2]}.json').read_text())
    tol = dict(vl.TOL); tol.update({'vib': 0.02, 'steps': 1, 'jitter': 0.3, 'click': 0.5, 'click_mean': 0.3})
    lines = ['| case | ' + ' | '.join(['m.' + m for m in METRICS] + ['c.pitch', 'c.formant'] + OWN + ['ins/smp']) + ' |',
             '|' + '---|' * (len(METRICS) + len(OWN) + 4)]
    bad = []
    for case in A:
        if case not in B:
            continue
        cells = []
        def cell(group, m, a, b):
            if a is None or b is None:
                return f'{a}->{b}'
            if m == 'vib':
                w = abs(b - 1) > abs(a - 1) + tol[m]
            elif m in ('click', 'click_mean'):
                w = max(b, 0) > max(a, 0) + tol[m]   # only an excess over the source is a click
            else:
                w = vl.worse(b, a, m, tol.get(m, 0.0))
            if w:
                bad.append(f'{case} {group}.{m} {a} -> {b}')
            return f'{a:g}→{b:g}' + (' **!**' if w else '')
        for m in METRICS:
            cells.append(cell('model', m, A[case]['model'][m], B[case]['model'][m]))
        cells.append(cell('centre', 'pitch', A[case]['centre']['pitch'], B[case]['centre']['pitch']))
        cells.append(cell('centre', 'formant', A[case]['centre']['formant'], B[case]['centre']['formant']))
        for m in OWN:
            cells.append(cell('own', m, A[case]['own'][m], B[case]['own'][m]))
        cells.append(f"{A[case]['cpu']['ins']:g}→{B[case]['cpu']['ins']:g}")
        lines.append(f'| {case} | ' + ' | '.join(cells) + ' |')
    print('\n'.join(lines))
    print('\nworse:', bad if bad else 'none')


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'compare':
        compare(sys.argv[2:])
    else:
        main(sys.argv[1:])
