#!/usr/bin/env python3
"""evaluate.py - LYNX Model's quality report (round 190ni): every recorded note against the engine's render of it.

  evaluate.py <physmodrender> <notes-root> <fit-dir> <out-dir> [--instruments a,b] [--sets held_out,extra,in_sample]
              [--tables DIR] [--keep-wavs]

<fit-dir> holds calibrate.py's output: <instrument>.lpm / .fit.json (the shipped tables, fitted on every note) and
<instrument>-holdout3.lpm / .fit.json (fitted without the held-out keys; with -holdout3f1 / -f2, every fold present,
--folds N: the first N); --tables: where the shipped .lpm files are
when the reports were written elsewhere (calibrate.py --report-dir). The model's sets per instrument:
  held_out   the held-out keys' takes, rendered from the holdout table (keys it never saw: the interpolation)
  extra      other takes of a key the table has (a second tine, a second hit; same key, a different take)
  in_sample  every fitted take, rendered from the shipped table (the fit itself)
and the references for the held-out notes (round 190nl; no render of the model's own sound is involved except ctl_key):
  sampler    the bar to beat: the nearest key of the holdout table's recordings at the same layer, transposed by
             resampling to the held-out key and retuned to the take's own tuning as the model's render is (a tuned
             multisample playing between its samples)
  ctl_key    control, must score low: the model at a shuffled key (another held-out note's key, else 5 semitones up)
  ctl_inst   control, must score low: another instrument's recording at the nearest key, transposed to the held-out key
             (a sampler of the wrong instrument)
Each render is the take's sounding key at its layer's velocity and the take's own tuning (cents of its main mode; the
model itself plays in tune), the model's preset values otherwise; the recording is
raised by the table's level offset so both sit at the same level. Scored with timbre_score.score_pair (and its wave
metrics, side by side). Writes <out-dir>/evaluation.json and evaluation.md. Renders are deleted unless --keep-wavs.
"""
import json
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import calibrate  # noqa: E402
import lpm  # noqa: E402
import modal_analysis as ma  # noqa: E402
import timbre_score as ts  # noqa: E402

CAP = {'tubularbells': 10.0, 'vibraphone': 8.0}


def tuning_of(x, sr, key):
    """cents of the take's main mode (the strongest spectral peak within 70 cents of the key, first second) re equal
    temperament, 0 if none. (Round 190nl: by the peak's magnitude, not a fitted decay: a vibraphone's fundamental rings
    for ~20 s, its decay cannot be fitted on a short segment, and the old rule then took a -50 dB leakage line 50 cents
    off as the main mode, so every vibraphone render was detuned by up to 53 cents.)"""
    f = calibrate.hz(key)
    near = [p for p in ma.pick_peaks(x[:int(1.5 * sr)], sr, 0.85 * f, None, 24, 12.0) if abs(calibrate.cents(p[0], f)) < 70]
    return float(calibrate.cents(max(near, key=lambda p: p[1])[0], f)) if near else 0.0


def render_all(exe, jobs, out_dir, rate):
    lst = os.path.join(out_dir, 'notes-%d.txt' % rate)
    with open(lst, 'w') as f:
        for j in jobs:
            f.write('%s %s %d %.4f %.3f cents=%.2f\n' % (j['wav'], j['table'], j['key'], j['velocity'], j['seconds'], j['cents']))
    r = subprocess.run([exe, 'notes', lst, '--rate', str(rate)], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit('physmodrender failed: %s %s' % (r.stdout, r.stderr))
    os.remove(lst)


def score_job(j, ref_root, keep):
    x, sr = ma.read_wav(os.path.join(ref_root, j['set'], j['file']))
    on = ma.onset(x, sr)
    x = x[max(0, on - int(0.001 * sr)):][:int(j['seconds'] * sr)] * 10 ** (j['offset_db'] / 20.0)
    y, sr2 = ma.read_wav(j['wav'])
    if not keep:
        os.remove(j['wav'])
    res = ts.score_pair(x, y, sr, f0=calibrate.hz(j['ref_key']) if j['instrument'] != 'tubularbells' else None, dur=j['seconds'])
    return {'file': j['file'], 'key': j['ref_key'], 'rendered_key': j['key'], 'velocity': j['velocity'], 'timbre_score': res['timbre_score'], 'timbre_score_190ni': res['timbre_score_190ni'],
            'subscores': {k: float(v) for k, v in res['subscores'].items()}, 'partials_matched': res['partials_matched'],
            'pitch_cents': res['pitch_cents'], 'level_db': res['level_db'], 'spectime_db': res.get('spectime_db'),
            'attack': res['attack'], 'decay_log2_median': res['decay_log2_median'],
            'wave': {k: (float(v) if v is not None else None) for k, v in (res['wave'] or {}).items()}}


def resample(y, semitones):
    """y played faster by 2^(semitones / 12) (linear interpolation): a sampler's transposition"""
    ratio = 2 ** (semitones / 12.0)
    idx = np.arange(0, len(y) - 1, ratio)
    i = idx.astype(int)
    fr = idx - i
    return y[i] * (1 - fr) + y[i + 1] * fr


def score_arrays(inst, name, file, key, velocity, x, y, sr, seconds):
    res = ts.score_pair(x[:int(seconds * sr)], y[:int(seconds * sr)], sr, f0=calibrate.hz(key) if inst != 'tubularbells' else None,
                        dur=seconds)
    return {'file': file, 'key': key, 'velocity': velocity, 'from': name, 'timbre_score': res['timbre_score'], 'timbre_score_190ni': res['timbre_score_190ni'],
            'subscores': {k: float(v) for k, v in res['subscores'].items()}, 'partials_matched': res['partials_matched'],
            'pitch_cents': res['pitch_cents'], 'level_db': res['level_db'], 'spectime_db': res.get('spectime_db'),
            'attack': res['attack'], 'decay_log2_median': res['decay_log2_median'],
            'wave': {k: (float(v) if v is not None else None) for k, v in (res['wave'] or {}).items()}}


def load_note(root, set_name, file):
    x, sr = ma.read_wav(os.path.join(root, set_name, file))
    return x[ma.onset(x, sr):], sr


def reference_sets(inst, root, hold, want):
    """the sampler baseline and the wrong-instrument control for the held-out takes (recordings only)"""
    cfg = calibrate.INSTRUMENTS[inst]
    used = [(n['key'], n['layer'], n['file']) for n in hold['notes']]
    out = {'sampler': [], 'ctl_inst': []}
    for t in hold['held_out_takes']:
        x, sr = load_note(root, cfg['set'], t['file'])
        secs = min(len(x) / sr - 0.05, CAP.get(inst, 6.0))
        vel = t['layer']  # (the layer's name: no velocity is rendered here)
        if 'sampler' in want:
            k, _, f = min([u for u in used if u[1] == t['layer']], key=lambda u: (abs(u[0] - t['key']), u[0]))
            y, _ = load_note(root, cfg['set'], f)
            # (a tuned multisample: the neighbour retuned to the take's own tuning, as the model's render is)
            tune = (tuning_of(x, sr, t['key']) - tuning_of(y, sr, k)) / 100.0
            out['sampler'].append(score_arrays(inst, f, t['file'], t['key'], vel, x, resample(y, t['key'] - k + tune), sr, secs))
        if 'ctl_inst' in want:
            # the other instruments' recordings (their fitted takes), the nearest key; ties: the instrument order
            best = None
            for other, ocfg in calibrate.INSTRUMENTS.items():
                if other == inst:
                    continue
                for n in json.load(open(os.path.join(os.path.dirname(hold['_path']), other + '.fit.json')))['notes']:
                    d = abs(n['key'] - t['key'])
                    if best is None or d < best[0]:
                        best = (d, ocfg['set'], n['file'], n['key'])
            _, oset, f, k = best
            y, sr2 = load_note(root, oset, f)
            # (transposed to the key and brought to the reference's rate in one resampling)
            y = resample(y, t['key'] - k + 12.0 * np.log2(sr2 / float(sr)))
            out['ctl_inst'].append(score_arrays(inst, oset + '/' + f, t['file'], t['key'], vel, x, y, sr, secs))
    return out


def summary(rows):
    if not rows:
        return None
    s = np.array([r['timbre_score'] for r in rows])
    w = [r['wave'].get('wave_score') for r in rows if r['wave'].get('wave_score') is not None]
    subs = {k: round(float(np.median([r['subscores'][k] for r in rows])), 2) for k in rows[0]['subscores']}
    s0 = np.array([r.get('timbre_score_190ni', np.nan) for r in rows])
    return {'notes': len(rows), 'timbre_median': round(float(np.median(s)), 2), 'timbre_190ni_median': round(float(np.median(s0)), 2),
            'timbre_190ni_mean': round(float(np.mean(s0)), 2), 'timbre_mean': round(float(s.mean()), 2),
            'timbre_min': round(float(s.min()), 2), 'wave_median': float(np.median(w)) if w else None, 'subscores_median': subs,
            'spec_db_median': round(float(np.median([r['wave']['spec_db'] for r in rows if r['wave'].get('spec_db') is not None] or [np.nan])), 2)}


def main():
    a = sys.argv[1:]
    if len(a) < 4:
        print(__doc__)
        return 2
    exe, root, fit, out = a[:4]
    tables = a[a.index('--tables') + 1] if '--tables' in a else fit  # (the shipped .lpm files: resources/physmod)
    keep = '--keep-wavs' in a
    insts = a[a.index('--instruments') + 1].split(',') if '--instruments' in a else list(calibrate.INSTRUMENTS)
    os.makedirs(out, exist_ok=True)
    report = {}
    for inst in insts:
        cfg = calibrate.INSTRUMENTS[inst]
        full = json.load(open(os.path.join(fit, inst + '.fit.json')))
        offset = lpm.read(open(os.path.join(tables, inst + '.lpm'), 'rb').read())['gainOffsetDb']
        # the holdout tables: <inst>-holdout3 and, when fitted, its other folds <inst>-holdout3f1, -f2 (round 190nl:
        # together they hold every inner recorded key out once)
        nl = len(cfg['layers'])
        vel = lambda layer: 0.8 if nl == 1 else (cfg['layers'].index(layer) + 1) / nl
        sets = {
            'held_out': [],
            'extra': [(t['file'], t['key'], vel(t['layer']), os.path.join(tables, inst + '.lpm'), offset) for t in full['extra_takes']],
            'in_sample': [(n['file'], n['key'], n['velocity'], os.path.join(tables, inst + '.lpm'), offset) for n in full['notes']],
            'ctl_key': [],
        }
        report[inst] = {}
        want = a[a.index('--sets') + 1].split(',') if '--sets' in a else list(sets) + ['sampler', 'ctl_inst']
        refrows = {}
        folds = [t for t in ('holdout3', 'holdout3f1', 'holdout3f2') if os.path.exists(os.path.join(fit, '%s-%s.fit.json' % (inst, t)))]
        if '--folds' in a:
            folds = folds[:int(a[a.index('--folds') + 1])]
        for tag in folds:
            hp = os.path.join(fit, '%s-%s.fit.json' % (inst, tag))
            tp = os.path.join(fit, '%s-%s.lpm' % (inst, tag))
            hold = json.load(open(hp))
            hold['_path'] = hp
            offset_h = lpm.read(open(tp, 'rb').read())['gainOffsetDb']
            sets['held_out'] += [(t['file'], t['key'], vel(t['layer']), tp, offset_h) for t in hold['held_out_takes']]
            hk = [t['key'] for t in hold['held_out_takes']]
            shuffled = [next((k for k in hk[i + 1:] + hk[:i] if k != hk[i]), hk[i] + 5) for i in range(len(hk))]
            sets['ctl_key'] += [(t['file'], k, vel(t['layer']), tp, offset_h, t['key']) for t, k in zip(hold['held_out_takes'], shuffled)]
            for name, rows in reference_sets(inst, root, hold, want).items():
                refrows.setdefault(name, []).extend(rows)
        for name, rows in refrows.items():
            if name in want:
                for r in rows:
                    print('  %-13s %-9s %-46s timbre %5.2f' % (inst, name, r['file'][:46], r['timbre_score']), flush=True)
                report[inst][name] = {'summary': summary(rows), 'notes': rows}
        for name, takes in sets.items():
            if name not in want:
                report[inst][name] = {'summary': None, 'notes': []}
                continue
            by_rate = {}
            for i, (f, key, v, table, off, *rk) in enumerate(takes):
                x, sr = ma.read_wav(os.path.join(root, cfg['set'], f))
                secs = min(len(x) / sr - 0.05, CAP.get(inst, 6.0))
                # the take's own tuning (the model plays in tune; the score compares timbre, not the take's tuning)
                # (round 190nl: the bells too - their strike tone, the strongest peak near the key, as calibrate.py takes it;
                # 190ni rendered them in tune against takes 4-14 cents sharp, while the sampler kept the takes' tuning)
                cents = tuning_of(x[ma.onset(x, sr):], sr, key) if name != 'ctl_key' else 0.0
                by_rate.setdefault(sr, []).append({'instrument': inst, 'set': cfg['set'], 'file': f, 'key': key, 'velocity': v,
                                                   'table': table, 'offset_db': off, 'seconds': secs, 'cents': cents, 'ref_key': rk[0] if rk else key,
                                                   'wav': os.path.join(out, '%s-%s-%03d.wav' % (inst, name, i))})
            rows = []
            for sr, jobs in by_rate.items():
                render_all(exe, jobs, out, sr)
                for j in jobs:
                    rows.append(score_job(j, root, keep))
                    print('  %-13s %-9s %-46s timbre %5.2f  wave %s' % (inst, name, j['file'][:46], rows[-1]['timbre_score'],
                                                                         rows[-1]['wave'].get('wave_score')), flush=True)
            report[inst][name] = {'summary': summary(rows), 'notes': rows}
        s = report[inst]
        med = lambda name: s.get(name, {}).get('summary') and s[name]['summary']['timbre_median']
        print('%s: held-out %s | sampler %s | extra %s | in-sample %s | ctl_key %s | ctl_inst %s' % (
            inst, med('held_out'), med('sampler'), med('extra'), med('in_sample'), med('ctl_key'), med('ctl_inst')), flush=True)
    json.dump(report, open(os.path.join(out, 'evaluation.json'), 'w'), indent=1)
    lines = ['# LYNX Model: timbre scores against the CC0 recordings', '',
             'Timbre score 0-10 (tools/physmod/timbre_score.py; phase-blind, per note), median (mean) per set; the wave score '
             '(scripts/fxparity.py, waveform null) beside it. held-out = keys the table never saw (holdout table); sampler = '
             'the nearest recorded key transposed (the bar to beat); ctl_key / ctl_inst = controls (wrong key / wrong '
             'instrument), must be low. Score rule 190nl (spectro-temporal part against the take\'s background); the 190ni '
             'rule beside it.', '',
             '| Instrument | held-out (n) | held-out min | sampler | extra takes (n) | in-sample (n) | ctl_key | ctl_inst | held-out, 190ni rule | sampler, 190ni rule | wave score, held-out | spec_db held-out |',
             '|---|---|---|---|---|---|---|---|---|---|---|---|']
    for inst, s in report.items():
        z = lambda name: s.get(name, {}).get('summary')
        f = lambda q: ('%.2f (%.2f, %d)' % (q['timbre_median'], q['timbre_mean'], q['notes'])) if q else '-'
        h = z('held_out')
        g = lambda q: ('%.2f (%.2f)' % (q['timbre_190ni_median'], q['timbre_190ni_mean'])) if q else '-'
        lines.append('| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |' % (
            inst, f(h), h['timbre_min'] if h else '-', f(z('sampler')), f(z('extra')), f(z('in_sample')), f(z('ctl_key')), f(z('ctl_inst')),
            g(h), g(z('sampler')), h['wave_median'] if h else '-', h['spec_db_median'] if h else '-'))
    for name in ('held_out', 'sampler', 'in_sample'):
        lines += ['', 'Sub-scores (median, %s):' % name, '']
        for inst, s in report.items():
            if s.get(name, {}).get('summary'):
                lines.append('- %s: %s' % (inst, ', '.join('%s %.1f' % kv for kv in s[name]['summary']['subscores_median'].items())))
    open(os.path.join(out, 'evaluation.md'), 'w').write('\n'.join(lines) + '\n')
    print('\n'.join(lines))
    return 0


if __name__ == '__main__':
    sys.exit(main())
