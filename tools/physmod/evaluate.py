#!/usr/bin/env python3
"""evaluate.py - LYNX Model's quality report (round 190ni): every recorded note against the engine's render of it.

  evaluate.py <physmodrender> <notes-root> <fit-dir> <out-dir> [--instruments a,b] [--sets held_out,extra,in_sample]
              [--tables DIR] [--keep-wavs]

<fit-dir> holds calibrate.py's output: <instrument>.lpm / .fit.json (the shipped tables, fitted on every note) and
<instrument>-holdout3.lpm / .fit.json (fitted without the held-out keys); --tables: where the shipped .lpm files are
when the reports were written elsewhere (calibrate.py --report-dir). Three sets per instrument:
  held_out   the held-out keys' takes, rendered from the holdout table (keys it never saw: the interpolation)
  extra      other takes of a key the table has (a second tine, a second hit; same key, a different take)
  in_sample  every fitted take, rendered from the shipped table (the fit itself)
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
    """cents of the take's main mode (the strongest within 70 cents of the key) re equal temperament, 0 if none"""
    f = calibrate.hz(key)
    near = [m for m in ma.analyse_modes(x[:int(1.5 * sr)], sr, fmin=0.85 * f, k=12) if abs(calibrate.cents(m['f'], f)) < 70]
    return float(calibrate.cents(max(near, key=lambda m: m['energy'])['f'], f)) if near else 0.0


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
    res = ts.score_pair(x, y, sr, f0=calibrate.hz(j['key']) if j['instrument'] != 'tubularbells' else None, dur=j['seconds'])
    return {'file': j['file'], 'key': j['key'], 'velocity': j['velocity'], 'timbre_score': res['timbre_score'],
            'subscores': {k: float(v) for k, v in res['subscores'].items()}, 'partials_matched': res['partials_matched'],
            'pitch_cents': res['pitch_cents'], 'level_db': res['level_db'], 'spectime_db': res.get('spectime_db'),
            'attack': res['attack'], 'decay_log2_median': res['decay_log2_median'],
            'wave': {k: (float(v) if v is not None else None) for k, v in (res['wave'] or {}).items()}}


def summary(rows):
    if not rows:
        return None
    s = np.array([r['timbre_score'] for r in rows])
    w = [r['wave'].get('wave_score') for r in rows if r['wave'].get('wave_score') is not None]
    subs = {k: round(float(np.median([r['subscores'][k] for r in rows])), 2) for k in rows[0]['subscores']}
    return {'notes': len(rows), 'timbre_median': round(float(np.median(s)), 2), 'timbre_mean': round(float(s.mean()), 2),
            'timbre_min': round(float(s.min()), 2), 'wave_median': float(np.median(w)) if w else None, 'subscores_median': subs,
            'spec_db_median': round(float(np.median([r['wave']['spec_db'] for r in rows])), 2)}


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
        hp = os.path.join(fit, inst + '-holdout3.fit.json')
        hold = json.load(open(hp)) if os.path.exists(hp) else {'held_out_takes': []}
        offset_h = lpm.read(open(os.path.join(fit, inst + '-holdout3.lpm'), 'rb').read())['gainOffsetDb'] if os.path.exists(hp) else 0.0
        nl = len(cfg['layers'])
        vel = lambda layer: 0.8 if nl == 1 else (cfg['layers'].index(layer) + 1) / nl
        sets = {
            'held_out': [(t['file'], t['key'], vel(t['layer']), os.path.join(fit, inst + '-holdout3.lpm'), offset_h) for t in hold['held_out_takes']],
            'extra': [(t['file'], t['key'], vel(t['layer']), os.path.join(tables, inst + '.lpm'), offset) for t in full['extra_takes']],
            'in_sample': [(n['file'], n['key'], n['velocity'], os.path.join(tables, inst + '.lpm'), offset) for n in full['notes']],
        }
        report[inst] = {}
        want = a[a.index('--sets') + 1].split(',') if '--sets' in a else list(sets)
        for name, takes in sets.items():
            if name not in want:
                report[inst][name] = {'summary': None, 'notes': []}
                continue
            by_rate = {}
            for i, (f, key, v, table, off) in enumerate(takes):
                x, sr = ma.read_wav(os.path.join(root, cfg['set'], f))
                secs = min(len(x) / sr - 0.05, CAP.get(inst, 6.0))
                # the take's own tuning (the model plays in tune; the score compares timbre, not the take's tuning)
                cents = tuning_of(x[ma.onset(x, sr):], sr, key) if inst != 'tubularbells' else 0.0
                by_rate.setdefault(sr, []).append({'instrument': inst, 'set': cfg['set'], 'file': f, 'key': key, 'velocity': v,
                                                   'table': table, 'offset_db': off, 'seconds': secs, 'cents': cents,
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
        print('%s: held-out %s | extra %s | in-sample %s' % (inst, s['held_out']['summary'] and s['held_out']['summary']['timbre_median'],
                                                             s['extra']['summary'] and s['extra']['summary']['timbre_median'],
                                                             s['in_sample']['summary']['timbre_median']), flush=True)
    json.dump(report, open(os.path.join(out, 'evaluation.json'), 'w'), indent=1)
    lines = ['# LYNX Model: timbre scores against the CC0 recordings', '',
             'Timbre score 0-10 (tools/physmod/timbre_score.py; phase-blind, per note), median per set; the wave score '
             '(scripts/fxparity.py, waveform null) beside it. held-out = keys the table never saw (holdout table).', '',
             '| Instrument | held-out (n) | held-out min | extra takes (n) | in-sample (n) | wave score, held-out | spec_db held-out |',
             '|---|---|---|---|---|---|---|']
    for inst, s in report.items():
        h, e, i = s['held_out']['summary'], s['extra']['summary'], s['in_sample']['summary']
        f = lambda z, k: ('%s (%d)' % (z[k], z['notes'])) if z else '-'
        lines.append('| %s | %s | %s | %s | %s | %s | %s |' % (inst, f(h, 'timbre_median'), h['timbre_min'] if h else '-', f(e, 'timbre_median'),
                                                          f(i, 'timbre_median'), h['wave_median'] if h else '-', h['spec_db_median'] if h else '-'))
    lines += ['', 'Sub-scores (median, held-out):', '']
    for inst, s in report.items():
        if s['held_out']['summary']:
            lines.append('- %s: %s' % (inst, ', '.join('%s %.1f' % kv for kv in s['held_out']['summary']['subscores_median'].items())))
    open(os.path.join(out, 'evaluation.md'), 'w').write('\n'.join(lines) + '\n')
    print('\n'.join(lines))
    return 0


if __name__ == '__main__':
    sys.exit(main())
