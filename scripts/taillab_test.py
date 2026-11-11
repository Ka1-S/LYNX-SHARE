#!/usr/bin/env python3
"""Unit tests of scripts/taillab.py on synthetic signals with known answers (round 190bp).

  python3 scripts/taillab_test.py        (or: python3 scripts/taillab.py selftest)

Every test builds its own signal: band-wise exponential noise decays with a known RT60, a capture shorter than
its tail, a two-slope decay, sparse and dense echo patterns, known L/R correlations, a static and a modulated
delay network, a pilot tone through a delay with a known LFO, and render pairs that are the same reverb with a
different phase (must score high on the tail score although the waveform score cannot).
"""
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fxparity  # noqa: E402
import taillab as tl  # noqa: E402

SR = 48000
FAILS = []
CHECKS = 0


def check(name, cond, info=''):
    global CHECKS
    CHECKS += 1
    if not cond:
        FAILS.append(f'{name} {info}')
        print('FAIL', name, info)


def near(name, got, want, rel):
    check(name, got is not None and abs(got - want) <= rel * abs(want), f'got {got} want {want} +-{rel * 100:.0f} %')


def stim_impulse(seconds=8.0, burst=True, seed=7):
    x = np.zeros((int(seconds * SR), 2))
    x[int(0.1 * SR)] = 0.5
    if burst:
        b = np.random.default_rng(seed).standard_normal(int(0.3 * SR)) * 0.25
        x[4 * SR:4 * SR + len(b)] = b[:, None]
    return x


def noise_ir(rt, seconds=3.5, seed=1, corr=0.0, gain=0.05):
    """Stereo noise impulse response whose octave bands decay with rt[k] seconds (RT60); L/R correlation `corr`."""
    rng = np.random.default_rng(seed)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    rts = rt if hasattr(rt, '__len__') else [rt] * len(tl.OCT)
    com = rng.standard_normal(n)
    out = []
    for c in (0, 1):
        w = np.sqrt(abs(corr)) * com * (1 if corr >= 0 or c == 0 else -1) + np.sqrt(1 - abs(corr)) * rng.standard_normal(n)
        b = tl.band_signals(w, SR)
        out.append(sum(b[k] * np.exp(-np.log(1000.0) * t / rts[k]) for k in range(len(tl.OCT))))
    return np.stack(out, 1) * gain


def render(ir, stim, dry=0.0):
    """stim convolved with ir (per channel) plus dry * stim."""
    n = len(stim)
    nf = 1 << int(np.ceil(np.log2(n + len(ir))))
    out = np.stack([np.fft.irfft(np.fft.rfft(stim[:, c], nf) * np.fft.rfft(ir[:, c], nf), nf)[:n] for c in (0, 1)], 1)
    return out + dry * stim


def modulated_net(stim, depth, rate=0.7, seed=3, taps=24, fb=0.0):
    """A time-variant test effect: `taps` delay taps (20-400 ms, random gains, random pan) whose delays move by
    +-`depth` samples at `rate` Hz with random phases. depth 0 = time-invariant."""
    rng = np.random.default_rng(seed)
    n = len(stim)
    t = np.arange(n)
    out = np.zeros((n, 2))
    m = stim.mean(1)
    for _ in range(taps):
        d0, g, ph, pan = rng.uniform(0.02, 0.4) * SR, rng.uniform(-1, 1) * 0.3, rng.uniform(0, 2 * np.pi), rng.uniform(0, 1)
        pos = t - (d0 + depth * np.sin(2 * np.pi * rate * t / SR + ph))
        y = np.interp(pos, t, m, left=0.0, right=0.0)
        out[:, 0] += g * pan * y
        out[:, 1] += g * (1 - pan) * y
    return out


def t_decay_known():
    rts = [2.4, 2.0, 1.6, 1.2, 1.0, 0.8, 0.6, 0.4]
    s = stim_impulse(burst=False)
    st = tl.impulse_stats(render(noise_ir(rts), s), s, SR)
    for k, c in enumerate(tl.OCT):
        near(f'T20 {c} Hz', st['t20'][k], rts[k], 0.08)
        near(f'T30 {c} Hz', st['t30'][k], rts[k], 0.08)
        near(f'EDT {c} Hz', st['edt'][k], rts[k], 0.20)
        near(f'late RT {c} Hz', st['rt_late'][k], rts[k], 0.15)
    check('pre-delay ~0', st['predelay_ms'] < 3.0, str(st['predelay_ms']))
    # the same with a dry path and 25 ms of pre-delay
    ir = np.concatenate([np.zeros((int(0.025 * SR), 2)), noise_ir(1.0)])
    st = tl.impulse_stats(render(ir, s, dry=0.35), s, SR)
    near('dry gain', st['dry'][0], 0.35, 0.01)
    near('pre-delay 25 ms', st['predelay_ms'], 25.0, 0.12)
    near('T30 with dry + pre-delay', st['t30'][4], 1.0, 0.08)
    # a plug-in latency of 64 samples: the dry path is found there and time zero moves with it
    late = np.concatenate([np.zeros((64, 2)), render(ir, s, dry=0.35)[:-64]])
    st = tl.impulse_stats(late, s, SR)
    check('latency found', st['latency'] == 64 and abs(st['dry'][0] - 0.35) < 0.004, f"{st['latency']} {st['dry']}")
    near('pre-delay unchanged by latency', st['predelay_ms'], 25.0, 0.12)
    wetonly = tl.impulse_stats(render(ir, s, dry=0.0), s, SR)
    check('no dry path: none reported', wetonly['dry'] == [0.0, 0.0] and wetonly['latency'] == 0, str(wetonly['dry']))


def t_decay_truncated():
    """RT 12 s seen through 3.9 s: T30 is out of range, the late-slope RT and EDT must still be right."""
    s = stim_impulse(burst=True)
    ir = noise_ir(12.0, seconds=7.5)
    st = tl.impulse_stats(render(ir, s), s, SR)
    check('T30 out of range', st['t30'][4] is None, str(st['t30'][4]))
    near('late RT truncated', st['rt_late'][4], 12.0, 0.15)
    near('EDT truncated', st['edt'][4], 12.0, 0.25)


def t_two_slopes():
    s = stim_impulse(burst=False)
    ir = noise_ir(0.3, gain=0.05) + noise_ir(3.0, seed=2, gain=0.05 * 10 ** (-25 / 20.0))
    st = tl.impulse_stats(render(ir, s), s, SR)
    check('EDT follows the first slope', st['edt'][4] is not None and st['edt'][4] < 0.8, str(st['edt'][4]))
    check('T30 sees the second slope', st['t30'][4] is not None and st['t30'][4] > 1.5, str(st['t30'][4]))
    one = tl.impulse_stats(render(noise_ir(3.0, seed=2), s), s, SR)
    sc = tl.impulse_score(one, st)
    check('relief/early punish a missing first slope', sc['parts']['early'] < 8.0 and sc['parts']['relief'] < 9.5, str(sc['parts']))


def t_echo_density():
    rng = np.random.default_rng(5)
    n = 2 * SR
    dense = rng.standard_normal(n)
    ned, _ = tl.ned_curve(dense, SR)
    near('NED of Gaussian noise', float(ned.mean()), 1.0, 0.06)
    sparse = np.zeros(n)
    sparse[rng.integers(0, n, 40)] = rng.standard_normal(40)                # 20 echoes per second
    ned, _ = tl.ned_curve(sparse, SR)
    check('NED of sparse echoes', float(ned.mean()) < 0.2, str(float(ned.mean())))
    # build-up: echo rate grows with t^2 (as in a room); dense from about 0.25 s on
    x = np.zeros(n)
    t = np.arange(n) / SR
    hit = rng.random(n) < np.minimum(1.0, 60.0 * t * t)
    x[hit] = rng.standard_normal(hit.sum())
    st = tl.ned_stats(x, SR)
    check('NED build-up times ordered', st['t50'] is not None and st['t90'] is not None and 0.01 < st['t50'] < st['t90'] < 0.5, str(st))
    fast = np.zeros(n)
    hit = rng.random(n) < np.minimum(1.0, 2000.0 * t * t)
    fast[hit] = rng.standard_normal(hit.sum())
    st2 = tl.ned_stats(fast, SR)
    check('faster build-up is seen', st2['t90'] < st['t90'] * 0.5, f"{st2['t90']} vs {st['t90']}")


def t_stereo():
    s = stim_impulse(burst=False)
    for want in (1.0, 0.5, 0.0, -0.6):
        st = tl.impulse_stats(render(noise_ir(1.5, corr=want), s), s, SR)
        got = np.mean([v for v in st['corr'][2] if v is not None])
        check(f'L/R correlation {want}', abs(got - want) < 0.08, str(got))
    wide = tl.impulse_stats(render(noise_ir(1.5, corr=0.0), s), s, SR)
    mono = tl.impulse_stats(render(noise_ir(1.5, corr=1.0), s), s, SR)
    check('width: mono is narrow', mono['width_db'][2] < -30 and abs(wide['width_db'][2]) < 2.0, f"{mono['width_db'][2]} {wide['width_db'][2]}")
    check('stereo part separates them', tl.impulse_score(wide, mono)['parts']['stereo'] < 1.0)


def t_time_variance():
    s = stim_impulse(burst=True)
    v = []
    for depth in (0.0, 2.0, 12.0, 60.0):
        st = tl.impulse_stats(modulated_net(s, depth), s, SR)
        v.append(np.mean([x for x in st['tvi']['during'] + st['tvi']['after'] if x is not None]))
    check('time-invariant network: index ~0', v[0] < 0.01, str(v))
    check('index grows with depth', v[0] < v[1] < v[2] <= v[3] + 0.05 and v[3] > 0.6, str(v))
    # high bands lose coherence first: at a small excursion 8 kHz is far above 250 Hz
    st = tl.impulse_stats(modulated_net(s, 2.0), s, SR)
    check('index is larger at high frequency', st['tvi']['during'][7] > st['tvi']['during'][2] + 0.1, str(st['tvi']['during']))
    st0 = tl.impulse_stats(render(noise_ir(1.5), s), s, SR)
    check('convolution reverb: index ~0', max(x for x in st0['tvi']['during'] + st0['tvi']['after'] if x is not None) < 0.01)


def t_pilot():
    n = 10 * SR
    t = np.arange(n)
    for hz, rate, depth in ((1000.0, 0.8, 20.0), (5000.0, 3.0, 4.0), (1000.0, 6.0, 1.0)):
        x = 0.25 * np.sin(2 * np.pi * hz * (t - (500 + depth * np.sin(2 * np.pi * rate * t / SR))) / SR)
        st = tl.pilot_stats(np.stack([x, x], 1), SR, hz, 1.0, 9.0)
        want = 1200.0 * np.log2(1.0 + 2 * np.pi * rate * depth / SR)
        near(f'pilot rate {rate} Hz', st['rate_hz'], rate, 0.03)
        near(f'pilot depth {want:.1f} cents', st['depth_cents'], want, 0.05)
    x = 0.25 * np.sin(2 * np.pi * 1000.0 * t / SR)
    st = tl.pilot_stats(np.stack([x, x], 1), SR, 1000.0, 1.0, 9.0)
    check('pilot: steady tone has no modulation', st['depth_cents'] < 0.01 and st['am_db'] < 0.001, str(st))
    y = x * (1 + 0.2 * np.sin(2 * np.pi * 2.0 * t / SR))
    st = tl.pilot_stats(np.stack([y, y], 1), SR, 1000.0, 1.0, 9.0)
    near('pilot: tremolo rate from the level', st['rate_hz'], 2.0, 0.03)


def _prog(seed=11, seconds=6.0):
    """Programme-like stimulus: decaying noise hits and tone bursts."""
    rng = np.random.default_rng(seed)
    n = int(seconds * SR)
    x = np.zeros(n)
    for k in range(int(seconds * 3)):
        i = int(k * SR / 3)
        m = min(int(0.15 * SR), n - i)
        tt = np.arange(m) / SR
        f = (110, 220, 330, 440, 660, 880)[k % 6] * rng.uniform(0.9, 1.1)
        tone = sum(np.sin(2 * np.pi * f * h * tt) / h for h in range(1, 12))
        x[i:i + m] += (0.3 * rng.standard_normal(m) * np.exp(-tt * 40) + 0.2 * tone * np.exp(-tt * 12))
    x[int((seconds - 1.5) * SR):] = 0
    return np.stack([x, x], 1)


def t_score_anchors():
    """The scale's anchors: the same reverb in another phase >= 9.2, decay + 10 % clearly below that, decay x 2 and
    an unrelated space lower still - while the waveform score cannot tell the first from the last."""
    rts = [2.0, 1.8, 1.5, 1.3, 1.1, 0.9, 0.7, 0.45]
    s = stim_impulse()
    prog = _prog()
    d = tempfile.mkdtemp(prefix='taillab_')
    cases = {'ref': noise_ir(rts, seed=1), 'same': noise_ir(rts, seed=2), 'rt110': noise_ir([r * 1.1 for r in rts], seed=3),
             'rt200': noise_ir([r * 2 for r in rts], seed=4), 'other': noise_ir([0.3] * 8, seed=5, corr=0.9, gain=0.2)}
    for name, ir in cases.items():
        ir = np.concatenate([np.zeros((48, 2)), ir])                     # 1 ms before the first reflection
        os.makedirs(f'{d}/{name}', exist_ok=True)
        tl.write_wav(f'{d}/{name}/D_impulse.wav', render(ir, s, dry=0.35), SR)
        tl.write_wav(f'{d}/{name}/D_music.wav', render(ir, prog, dry=0.35), SR)
    os.makedirs(f'{d}/stim', exist_ok=True)
    tl.write_wav(f'{d}/stim/impulse.wav', s, SR)
    tl.write_wav(f'{d}/stim/music.wav', prog, SR)
    res = {k: tl.score_dir(f'{d}/ref', f'{d}/{k}', f'{d}/stim') for k in cases}
    t = {k: v['tail'] for k, v in res.items()}
    print('   anchors (tail | wave):', ' '.join(f"{k} {v['tail']} | {v['wave']}" for k, v in res.items()))
    if os.environ.get('VERBOSE'):
        for k, v in res.items():
            print('    ', k, {n: (f['score'], f['parts']) for n, f in v['files'].items()})
    check('identical files score 10', t['ref'] >= 9.99, str(t))
    # noise-built reverbs on a short synthetic programme are the worst case of realisation noise; the real banks'
    # repeated recordings of one reference state score 9.7 ... 10 on their programme files (BASELINE-TAIL.md, anchors)
    check('same reverb, other phase >= 9.2', t['same'] >= 9.2, str(t))
    check('decay +10 % sits below, 8 ... 9.6', 8.0 <= t['rt110'] <= 9.6 and t['rt110'] < t['same'] - 0.2, str(t))
    check('decay x 2: 4 ... 6.5', 4.0 <= t['rt200'] <= 6.5, str(t))
    check('unrelated space <= 3', t['other'] <= 3.0 and t['other'] < t['rt200'], str(t))
    check('wave score cannot rank them', res['same']['wave'] <= 6 and abs(res['same']['wave'] - res['rt110']['wave']) <= 1.0,
          f"{res['same']['wave']} {res['rt110']['wave']}")
    check('dry gain found on both sides', abs(res['same']['settings']['D']['dry_ref'][0] - 0.35) < 0.01)
    # loss table: for the decay x 2 case the decay part must be the largest single loss of the impulse half
    imp_loss = {k: v for k, v in res['rt200']['loss'].items() if k.startswith('imp.')}
    check('loss table names decay first', next(iter(imp_loss)) in ('imp.decay', 'imp.relief'), str(imp_loss))
    near('losses add up to 10 - tail', sum(res['rt200']['loss'].values()), 10.0 - res['rt200']['tail'], 0.02)
    # polarity and a 1-sample shift do not matter
    a, _ = fxparity.read_wav(f'{d}/ref/D_impulse.wav')
    os.makedirs(f'{d}/flip', exist_ok=True)
    w = a - 0.35 * s
    tl.write_wav(f'{d}/flip/D_impulse.wav', np.concatenate([np.zeros((1, 2)), -w[:-1]]) + 0.35 * s, SR)
    r = tl.pair(f'{d}/ref/D_impulse.wav', f'{d}/flip/D_impulse.wav', f'{d}/stim/impulse.wav', g_ref=(0.35, 0.35, 0), g_lynx=(0.35, 0.35, 0))
    check('polarity + 1 sample: >= 9.8', r['score'] >= 9.8, str(r['score']) + str(r['parts']))
    for name in cases:
        for f in os.listdir(f'{d}/{name}'):
            os.remove(f'{d}/{name}/{f}')


def t_motion():
    """kind mod: an auto-pan / tremolo at another rate must lose the motion part, another LFO phase must not."""
    prog = _prog(seconds=8.0)
    n = len(prog)
    t = np.arange(n) / SR
    rng = np.random.default_rng(2)
    x = prog + 0.05 * rng.standard_normal((n, 1)) * (t < 6.5)[:, None]

    def pan(rate, ph):
        p = 0.5 + 0.45 * np.sin(2 * np.pi * rate * t + ph)
        return np.stack([x[:, 0] * np.sqrt(1 - p), x[:, 1] * np.sqrt(p)], 1)
    A = tl.programme_stats(pan(1.0, 0.0), x, SR, kind='mod')
    same = tl.programme_score(A, tl.programme_stats(pan(1.0, 2.0), x, SR, kind='mod'), 'mod')
    fast = tl.programme_score(A, tl.programme_stats(pan(4.0, 0.0), x, SR, kind='mod'), 'mod')
    still = tl.programme_score(A, tl.programme_stats(x * 0.7, x, SR, kind='mod'), 'mod')
    check('motion: other phase, same rate >= 9', same['parts']['motion'] >= 9.0, str(same['parts']))
    check('motion: 4x rate is punished', fast['parts']['motion'] <= same['parts']['motion'] - 2.0, str(fast['parts']))
    check('motion: no pan at all is punished most', still['parts']['motion'] <= fast['parts']['motion'], str(still['parts']))


def run():
    for fn in (t_decay_known, t_decay_truncated, t_two_slopes, t_echo_density, t_stereo, t_time_variance, t_pilot,
               t_score_anchors, t_motion):
        print(fn.__name__)
        fn()
    print(f'taillab selftest: {CHECKS} checks, {len(FAILS)} failures')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(run())
