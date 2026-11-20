#!/usr/bin/env python3
"""refine.py - LYNX Model's strike refinement through the real engine (round 190nl).

calibrate.py fits the strike (the contact time constant, the resonator build-up, the strike noise's level, centre, Q
and decay) through a closed-form emulation of the engine's first 150 ms. That emulation leaves the contact at the
ends of its range for many notes (0.03 or 2 ms in neighbouring keys), so a key between two recorded ones interpolated a
strike neither had: the held-out notes' attacks rose 4x too fast. This tool refines those parameters by rendering
candidates with the engine itself (tools/physmodrender, one single-anchor table per candidate) and scoring them with
the timbre score's own attack and spectro-temporal parts (timbre_score.StrikeLoss), coordinate by coordinate over
absolute grids, with a smoothness term that pulls each anchor towards its layer's neighbours (so the parameters stay
interpolable between recorded keys).

  refine.py <physmodrender> <notes-root> <fit.json> <table.lpm> <out.lpm> [--noise-order 1|2] [--sweeps N]
            [--smooth L] [--report OUT.json] [--work DIR] [--inc-dir DIR] [--level]
        --level (round 190nm): then each anchor's level matched through the engine (its first 2 s against its recording,
        as the timbre score's level part measures them)
        --inc-dir: also write <instrument>.inc there (src/core/physmod/tables/, the table LYNX compiles in)

Only parameters change; modes stay as fitted. The output table keeps the input's header (position mapping, level
offset, provenance hash), with the noise order set.
"""
import json
import os
import shutil
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import calibrate  # noqa: E402
import lpm  # noqa: E402
import modal_analysis as ma  # noqa: E402
import timbre_score as ts  # noqa: E402

# parameter: (grid, kind, unit of the smoothness term)
GRIDS = {
    'contact': (list(np.geomspace(0.00003, 0.004, 10)), 'log', 1.0),
    'riseMs': ([0.0, 2.0, 4.0, 8.0, 16.0, 32.0], 'lin', 8.0),
    'noiseDb': ([-60.0] + list(np.arange(-40.0, 21.0, 5.0)), 'lin', 6.0),
    'noiseHz': (None, 'log', 1.0),  # (per sample rate: 150 Hz ... 0.4 fs)
    'noiseQ': ([0.5, 0.7, 1.0, 1.4, 2.0, 3.0, 4.5], 'log', 1.0),
    'noiseMs': ([1.5, 3.0, 6.0, 12.0, 25.0, 50.0, 100.0], 'log', 1.0),
}
ORDER = ['contact', 'noiseDb', 'noiseHz', 'noiseMs', 'noiseQ', 'riseMs']
SECONDS = 1.0


def pval(name, v):
    kind, unit = GRIDS[name][1], GRIDS[name][2]
    return (np.log2(max(v, 1e-9)) if kind == 'log' else v) / unit


def render(exe, work, jobs, sr):
    """jobs: (anchor, header, tuning cents) -> list of float arrays"""
    lst = os.path.join(work, 'list.txt')
    lines = []
    for i, (a, head, cents) in enumerate(jobs):
        tp = os.path.join(work, 'c%05d.lpm' % i)
        lpm.write(tp, head['model'], head['beam'], head['posRef'], head['posU0'], head['posU1'], head['gainOffsetDb'], [a], b'',
                  head['noiseOrder'], head.get('onset', 0), head.get('fade', 0))
        lines.append('%s %s %d %.4f %.3f cents=%.2f' % (os.path.join(work, 'c%05d.wav' % i), tp, int(round(a['key'])), a['velocity'],
                                                          SECONDS, cents))
    open(lst, 'w').write('\n'.join(lines) + '\n')
    r = subprocess.run([exe, 'notes', lst, '--rate', str(sr)], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit('physmodrender failed: %s %s' % (r.stdout[-400:], r.stderr[-400:]))
    out = []
    for i in range(len(jobs)):
        y, _ = ma.read_wav(os.path.join(work, 'c%05d.wav' % i))
        out.append(y)
        os.remove(os.path.join(work, 'c%05d.wav' % i))
        os.remove(os.path.join(work, 'c%05d.lpm' % i))
    os.remove(lst)
    return out


def neighbours(anchors, i):
    """the same layer's nearest anchors below and above"""
    a = anchors[i]
    same = [j for j, b in enumerate(anchors) if j != i and abs(b['velocity'] - a['velocity']) < 1e-4]
    lo = [j for j in same if anchors[j]['key'] < a['key']]
    hi = [j for j in same if anchors[j]['key'] > a['key']]
    return ([max(lo, key=lambda j: anchors[j]['key'])] if lo else []) + ([min(hi, key=lambda j: anchors[j]['key'])] if hi else [])


def main():
    a = sys.argv[1:]
    if len(a) < 5:
        print(__doc__)
        return 2
    exe, root, fitp, tin, tout = a[:5]
    opt = lambda k, d: type(d)(a[a.index(k) + 1]) if k in a else d
    order = opt('--noise-order', 2)
    sweeps = opt('--sweeps', 2)
    smooth = opt('--smooth', 0.01)
    work = opt('--work', os.path.join(os.path.dirname(os.path.abspath(tout)), 'refine-work-%d' % os.getpid()))
    os.makedirs(work, exist_ok=True)
    fit = json.load(open(fitp))
    head = lpm.read(open(tin, 'rb').read())
    head['noiseOrder'] = order
    inst = fit['instrument']
    cfg = calibrate.INSTRUMENTS[inst]
    anchors = [dict(x) for x in head['anchors']]
    for x in anchors:
        x['modes'] = [dict(m) for m in x['modes']]
    notes = {(float(n['key']), round(float(n['velocity']), 4)): n for n in fit['notes']}
    refs, sr = [], None
    for x in anchors:
        n = notes[(x['key'], round(x['velocity'], 4))]
        y, rate = ma.read_wav(os.path.join(root, cfg['set'], n['file']))
        sr = sr or rate
        y = y[ma.onset(y, rate):][:int(min(cfg['seconds'], 8.0) * rate)]
        refs.append((ts.StrikeLoss(y, rate), n.get('tuning_offset_cents', 0.0)))
    GRIDS['noiseHz'] = (list(np.geomspace(150.0, 0.4 * sr, 11)), 'log', 1.0)
    if head.get('onset', 0) in (1, 2):
        # (round 190nm: riseMs is the whole note's onset there - a 10-90 % rise of about 2.2 x rise (2.6 x for the
        # S-shaped onset 2, round 190nn) - finer steps)
        GRIDS['riseMs'] = ([0.0, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0], 'lin', 1.0)

    def losses(cands):
        """cands: list of (anchor index, anchor dict) -> losses"""
        ys = render(exe, work, [(c, head, refs[i][1]) for i, c in cands], sr)
        return [refs[i][0].loss(y) for (i, _), y in zip(cands, ys)]

    start = losses(list(enumerate(anchors)))
    hist = []
    for sweep in range(sweeps):
        for name in ORDER:
            grid = GRIDS[name][0]
            prev = [x[name] for x in anchors]
            cands = []
            for i, x in enumerate(anchors):
                for v in grid:
                    c = dict(x)
                    c[name] = float(v)
                    cands.append((i, c))
            ls = losses(cands)
            k = 0
            for i, x in enumerate(anchors):
                nb = [prev[j] for j in neighbours(anchors, i)]
                best = None
                for v in grid:
                    pen = smooth * np.mean([(pval(name, v) - pval(name, w)) ** 2 for w in nb]) if nb else 0.0
                    tot = ls[k] + pen
                    if best is None or tot < best[0]:
                        best = (tot, v, ls[k])
                    k += 1
                x[name] = float(best[1])
            hist.append({'sweep': sweep, 'param': name})
        print('  %s sweep %d done' % (inst, sweep + 1), flush=True)
    if '--level' in a:
        # (round 190nm) each anchor's level matched through the engine: the first 2 s of its render against its recording
        # raised by the table's level offset, as the timbre score's level part measures them (a fitted main mode's
        # level misses a two-stage decay's loud start, or a fast mode's)
        global SECONDS
        keep, SECONDS = SECONDS, 2.5
        ys = render(exe, work, [(x, head, refs[i][1]) for i, x in enumerate(anchors)], sr)
        SECONDS = keep
        g = 10 ** (head['gainOffsetDb'] / 20.0)
        for i, (x, y) in enumerate(zip(anchors, ys)):
            r = refs[i][0].r * g
            c = ts.align(r, ts.prepare(np.asarray(y, np.float64), sr, len(r) / sr), sr)
            n = min(len(r), len(c), int(2 * sr))
            lev = 10 * np.log10((np.mean(c[:n] ** 2) + 1e-30) / (np.mean(r[:n] ** 2) + 1e-30))
            x['gainDb'] = float(x['gainDb'] - np.clip(lev, -24.0, 24.0))
    end = losses(list(enumerate(anchors)))
    lpm.write(tout, head['model'], head['beam'], head['posRef'], head['posU0'], head['posU1'], head['gainOffsetDb'], anchors,
              b'', order, head.get('onset', 0), head.get('fade', 0))
    # (the provenance hash: lpm.write hashed b''; keep the input's)
    data = bytearray(open(tout, 'rb').read())
    data[32:40] = bytes.fromhex(head['provenance_sha256_8'])
    open(tout, 'wb').write(bytes(data))
    rep = {'instrument': inst, 'table_in': tin, 'table_out': tout, 'noise_order': order, 'sweeps': sweeps, 'smooth': smooth,
           'loss_start_mean': round(float(np.mean(start)), 4), 'loss_end_mean': round(float(np.mean(end)), 4),
           'anchors': [{'key': x['key'], 'velocity': x['velocity'], 'loss_start': round(s0, 4), 'loss_end': round(s1, 4),
                        **{k: x[k] for k in ORDER}} for x, s0, s1 in zip(anchors, start, end)]}
    if '--inc-dir' in a:
        calibrate.write_inc(inst, bytes(data), opt('--inc-dir', ''))
    if '--report' in a:
        json.dump(rep, open(opt('--report', ''), 'w'), indent=1)
    print('%s: strike loss %.4f -> %.4f (%d anchors)' % (inst, rep['loss_start_mean'], rep['loss_end_mean'], len(anchors)), flush=True)
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
