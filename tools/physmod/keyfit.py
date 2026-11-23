#!/usr/bin/env python3
"""keyfit.py - LYNX Model's mode tables across keys (round 190nl): mode families and smooth fits against key.

A table holds one anchor per recorded key and velocity layer (calibrate.py). Between anchors the engine blends the two
neighbours (src/core/physmod/LynxModel.cpp blend()); before round 190nl it paired modes by frequency ratio within 4 %,
so a mode whose ratio drifts across a 5-12 semitone gap (a glockenspiel's second mode moves 3.04 -> 2.88 over an
octave) fell apart into two half-level modes at the wrong frequencies. This module:

  families(anchors)            tags each mode of each layer with a family id (u8, 1-254; 0 = none; 255 = a line at
                               its own pitch, left alone): the same physical mode in neighbouring anchors, matched by
                               log ratio within a tolerance that grows with the key gap plus their level difference,
                               lowest cost first, one to one. The engine pairs modes by family when both carry one
                               (the 4 % rule stays for untagged modes and tables).
  densify(anchors, ...)        adds synthetic anchors inside gaps wider than `max_gap` semitones: each family's log
                               ratio, level and log T60 (and each anchor scalar: level, contact, noise, build-up) at the
                               new key from a smooth fit against key (Gaussian-weighted local linear regression over the
                               family's anchors, width `sigma` semitones) plus the two neighbours' residuals, linearly
                               blended and shrunk towards the fit in the middle of the gap by `shrink` (1 - shrink * 4 t (1 - t)).
                               At a recorded key nothing changes; the recorded anchors stay exactly as fitted.
  choose(anchors, ...)         sigma and shrink per quantity by leave-one-out over the table's own anchors (each inner
                               anchor predicted from the others), so no held-out note is involved in the choice.

densify and choose are lab tools: round 190nl tried synthetic anchors (every semitone, linear and LOO-chosen fits) on the
three held-out folds and found no gain over the engine's family-aware blend of the recorded anchors, so the shipped
tables hold recorded anchors only (calibrate.py uses families()).

Only parameters: a synthetic anchor is a prediction from fitted parameters, never audio.
"""
import numpy as np

ABSOLUTE = 255  # (lpm.ABSOLUTE: a line at its own pitch, never part of a family)
SCALARS = [('gainDb', 'lin'), ('contact', 'log'), ('riseMs', 'lin'), ('noiseDb', 'lin'), ('noiseHz', 'log'), ('noiseQ', 'log'),
           ('noiseMs', 'log')]
QUANT = ('ratio', 'ampDb', 't60')


def _layers(anchors):
    out = {}
    for a in anchors:
        out.setdefault(round(a['velocity'], 4), []).append(a)
    for v in out:
        out[v].sort(key=lambda a: a['key'])
    return out


def families(anchors, tol=0.03, per_semitone=0.003, tol_cap=0.08, level_db=24.0):
    """sets m['fam'] (1-255) on every mode; modes of one layer's neighbouring anchors that are the same mode share it.
    A pair's cost is its log-ratio distance re the tolerance plus its level difference re `level_db` (a strong mode
    continues as a strong mode, not as a weak line that happens to sit closer), lowest cost first, one to one.
    (round 190nm: the main modes - ratio 1, the key's tuned mode - always pair with each other; and the working ids
    start at 1000, so a family can no longer take the id 255 of the absolute lines before the final ids are given: with
    more than 254 families a table had regular modes marked absolute)"""
    absolute = {id(m) for a in anchors for m in a['modes'] if m.get('fam') == ABSOLUTE}
    is_main = lambda m: abs(np.log(m['ratio'])) < 1e-3
    next_id = 1000
    for layer in _layers(anchors).values():
        prev = None
        for a in layer:
            for m in a['modes']:
                m['fam'] = 0
            if prev is not None:
                lim = min(tol + per_semitone * abs(a['key'] - prev['key']), tol_cap)
                pairs = []
                for i, p in enumerate(prev['modes']):
                    if id(p) in absolute:
                        continue
                    for j, q in enumerate(a['modes']):
                        if id(q) in absolute:
                            continue
                        if is_main(p) and is_main(q):
                            pairs.append((-1.0, i, j))
                            continue
                        d = abs(np.log(q['ratio'] / p['ratio']))
                        if d < lim:
                            cost = d / lim + abs(q['ampDb'] - p['ampDb']) / level_db
                            if p.get('part') or q.get('part'):
                                # (round 190nm: the parts of a split mode - a two-stage decay, a beating pair - continue
                                # as the same part: like decay, like role)
                                cost += abs(np.log2(q['t60'] / p['t60'])) / 3.0 + (0.5 if p.get('part', 0) != q.get('part', 0) else 0.0)
                            pairs.append((cost, i, j))
                pairs.sort()
                ui, uj = set(), set()
                for d, i, j in pairs:
                    if i in ui or j in uj:
                        continue
                    ui.add(i)
                    uj.add(j)
                    a['modes'][j]['fam'] = prev['modes'][i]['fam']
            for m in a['modes']:
                if m['fam'] == 0 and id(m) not in absolute:
                    m['fam'] = next_id
                    next_id += 1
            prev = a
    # ids are u8: families seen in a single anchor only need none (the engine then uses the 4 % rule); the others get
    # ids 1..254, each the smallest id no other family uses at its keys or the neighbouring recorded keys, in ANY layer
    # (the engine compares neighbouring keys, and a note between two layers carries modes of both: two families that can
    # meet never share an id; further away an id may repeat); absolute lines get 255
    count = {}
    for a in anchors:
        for m in a['modes']:
            if id(m) not in absolute:
                count[m['fam']] = count.get(m['fam'], 0) + 1
    keys = sorted(set(a['key'] for a in anchors))
    kidx = {k: i for i, k in enumerate(keys)}
    where = {}
    for a in anchors:
        for m in a['modes']:
            if id(m) not in absolute and count[m['fam']] >= 2:
                where.setdefault(m['fam'], set()).add(kidx[a['key']])
    taken = [dict() for _ in keys]  # per recorded key: id -> family
    remap = {}
    for f in sorted(where, key=lambda f: (min(where[f]), f)):
        near = set()
        for j in where[f]:
            for k in (j - 1, j, j + 1):
                if 0 <= k < len(keys):
                    near |= set(taken[k])
        new_id = next((i for i in range(1, 255) if i not in near), None)
        if new_id is None:
            continue  # (no id left: the family stays untagged)
        remap[f] = new_id
        for j in where[f]:
            taken[j][new_id] = f
    for a in anchors:
        for m in a['modes']:
            m['fam'] = ABSOLUTE if id(m) in absolute else remap.get(m['fam'], 0)
    return anchors


def _val(m, q):
    return np.log(m[q]) if q in ('ratio', 't60') else m[q]


def _scal(a, name, kind):
    v = a.get(name, 0.0)
    return np.log(max(v, 1e-9)) if kind == 'log' else v


def _trend(keys, vals, k, sigma):
    """Gaussian-weighted local linear regression of vals against keys, evaluated at k"""
    keys, vals = np.asarray(keys, float), np.asarray(vals, float)
    if len(keys) == 1:
        return float(vals[0])
    w = np.ones(len(keys)) if sigma is None else np.exp(-0.5 * ((keys - k) / sigma) ** 2) + 1e-9
    A = np.vstack([np.ones(len(keys)), keys - k]).T * np.sqrt(w)[:, None]
    sol, *_ = np.linalg.lstsq(A, vals * np.sqrt(w), rcond=None)
    return float(sol[0])


def _predict(points, k, k0, v0, k1, v1, sigma, shrink):
    """value at k (k0 < k < k1) from the fit over `points` (key, value) plus the neighbours' residuals"""
    keys = [p[0] for p in points]
    vals = [p[1] for p in points]
    t = (k - k0) / (k1 - k0)
    f = _trend(keys, vals, k, sigma)
    r0 = v0 - _trend(keys, vals, k0, sigma)
    r1 = v1 - _trend(keys, vals, k1, sigma)
    return f + ((1 - t) * r0 + t * r1) * (1.0 - shrink * 4.0 * t * (1.0 - t))


def _family_points(layer, fam, q):
    pts = []
    for a in layer:
        for m in a['modes']:
            if m.get('fam') == fam:
                pts.append((a['key'], _val(m, q)))
                break
    return pts


def synth_anchor(layer, j, k, params, max_modes, absolute=False):
    """an anchor at key k between layer[j] and layer[j + 1]. absolute: a mode only one side has keeps its frequency
    in Hz rather than its ratio to the key (lines of the instrument's other bars or tines ringing along sit at their own
    pitch whatever key is struck)"""
    a, b = layer[j], layer[j + 1]
    t = (k - a['key']) / (b['key'] - a['key'])
    move = lambda r, k0: r * 2 ** ((k0 - k) / 12.0) if absolute else r
    new = {'key': float(k), 'velocity': a['velocity'], 'synthetic': True}
    for name, kind in SCALARS:
        pts = [(x['key'], _scal(x, name, kind)) for x in layer]
        sg, sh = params.get(name, (None, 0.0))
        v = _predict(pts, k, a['key'], _scal(a, name, kind), b['key'], _scal(b, name, kind), sg, sh)
        new[name] = float(np.exp(v)) if kind == 'log' else float(v)
    new['riseMs'] = max(new['riseMs'], 0.0)
    modes = []
    bf = {m['fam']: m for m in b['modes'] if m.get('fam')}
    used = set()
    for m in a['modes']:
        f = m.get('fam')
        if f and f in bf:
            used.add(f)
            q = {'shape': m['shape'] or bf[f]['shape'], 'fam': f}
            for name in QUANT:
                sg, sh = params.get(name, (None, 0.0))
                v = _predict(_family_points(layer, f, name), k, a['key'], _val(m, name), b['key'], _val(bf[f], name), sg, sh)
                q[name] = float(np.exp(v)) if name in ('ratio', 't60') else float(v)
            modes.append(q)
        else:  # (only on this side: it fades out across the gap, as the engine's blend does)
            modes.append({'ratio': move(m['ratio'], a['key']), 'ampDb': m['ampDb'] + 20 * np.log10(max(1 - t, 1e-3)), 't60': m['t60'],
                          'shape': 0 if absolute else m['shape'], 'fam': f or 0})
    for m in b['modes']:
        f = m.get('fam')
        if not (f and f in used):
            modes.append({'ratio': move(m['ratio'], b['key']), 'ampDb': m['ampDb'] + 20 * np.log10(max(t, 1e-3)), 't60': m['t60'],
                          'shape': 0 if absolute else m['shape'], 'fam': f or 0})
    modes.sort(key=lambda m: -(10 ** (m['ampDb'] / 10)) * min(m['t60'], 3.0))
    new['modes'] = sorted(modes[:max_modes], key=lambda m: m['ratio'])
    # (the main mode keeps ratio 1 exactly: the note's pitch)
    main = min(new['modes'], key=lambda m: abs(np.log(m['ratio'])))
    if abs(np.log(main['ratio'])) < 0.01:
        main['ratio'] = 1.0
        main['ampDb'] = 0.0
    return new


def densify(anchors, max_gap=2.0, step=None, params=None, max_modes=28, absolute=False):
    """the anchors plus synthetic ones inside each layer's gaps wider than max_gap semitones (at every `step` semitones,
    default: as evenly as possible with spacing <= max_gap)"""
    params = params or {}
    out = list(anchors)
    for layer in _layers(anchors).values():
        for j in range(len(layer) - 1):
            k0, k1 = layer[j]['key'], layer[j + 1]['key']
            gap = k1 - k0
            if gap <= max_gap:
                continue
            n = int(np.ceil(gap / (step or max_gap))) - 1
            for i in range(1, n + 1):
                k = k0 + gap * i / (n + 1)
                k = round(k * 2) / 2.0  # (half semitones: a held-out key lands on an anchor or between two)
                if k <= k0 or k >= k1:
                    continue
                out.append(synth_anchor(layer, j, k, params, max_modes, absolute))
    # (duplicates from rounding: keep the first)
    seen, res = set(), []
    for a in sorted(out, key=lambda a: (a['key'], a['velocity'])):
        if (a['key'], a['velocity']) in seen:
            continue
        seen.add((a['key'], a['velocity']))
        res.append(a)
    return res


def choose(anchors, sigmas=(3.0, 6.0, 12.0, None), shrinks=(0.0, 0.5, 1.0)):
    """sigma and shrink per quantity by leave-one-out over the anchors (inner anchors of each layer, predicted from the
    anchors left after removing them); errors: levels in dB, ratios in cents, T60s and log scalars in log2, weighted by
    each mode's energy. Returns ({quantity: (sigma, shrink)}, {quantity: best error})"""
    layers = list(_layers(anchors).values())
    best, errs = {}, {}
    for name in list(QUANT) + [s[0] for s in SCALARS]:
        scores = {}
        for sg in sigmas:
            for sh in shrinks:
                e, wsum = 0.0, 0.0
                for layer in layers:
                    for j in range(1, len(layer) - 1):
                        rest = layer[:j] + layer[j + 1:]
                        a, b, x = layer[j - 1], layer[j + 1], layer[j]
                        if name in QUANT:
                            bf = {m['fam']: m for m in b['modes'] if m.get('fam')}
                            for m in a['modes']:
                                f = m.get('fam')
                                mx = next((y for y in x['modes'] if f and y.get('fam') == f), None)
                                if not f or f not in bf or mx is None:
                                    continue
                                pts = [(r['key'], _val(y, name)) for r in rest for y in r['modes'] if y.get('fam') == f]
                                v = _predict(pts, x['key'], a['key'], _val(m, name), b['key'], _val(bf[f], name), sg, sh)
                                w = np.sqrt(10 ** (mx['ampDb'] / 10) * min(mx['t60'], 3.0))
                                d = abs(v - _val(mx, name))
                                d = d * 1200 / np.log(2) if name == 'ratio' else d / np.log(2) if name == 't60' else d
                                e += w * min(d, 30.0)
                                wsum += w
                        else:
                            kind = dict(SCALARS)[name]
                            pts = [(r['key'], _scal(r, name, kind)) for r in rest]
                            v = _predict(pts, x['key'], a['key'], _scal(a, name, kind), b['key'], _scal(b, name, kind), sg, sh)
                            d = abs(v - _scal(x, name, kind))
                            e += d / np.log(2) if kind == 'log' else d
                            wsum += 1.0
                scores[(sg, sh)] = e / max(wsum, 1e-12)
        k = min(scores, key=lambda q: (round(scores[q], 6), q[1], -(q[0] or 1e9)))
        best[name] = k
        errs[name] = {'best': round(scores[k], 4), 'linear': round(scores.get((None, 0.0), np.nan), 4)}
    return best, errs
