#!/usr/bin/env python3
"""fxparity.py — sound parity of LYNX's effects against the third-party originals they are inspired by (round tp172b).

The originals are rendered black-box by tools/fxmeasure (a host, public parameter API only) and LYNX by
tools/fxrender, on the same stimuli at matched settings. Each pair of renders is compared exactly like FL's own render
tests (scripts/flref_compare.py: alignment, ESR, level, third-octave spectrum, 50 ms envelope) and scored 0-10 with
the same scale as the FL-native parity tracker (round 170i).

Usage:
  fxparity.py pair <original.wav> <lynx.wav>        metrics + score as JSON
  fxparity.py dir <original-dir> <lynx-dir>         every <name>.wav present in both, one JSON line each
  fxparity.py verb <original.wav> <lynx.wav> [t0]   reverb score of two impulse renders (impulse at t0 s, default
                                                    0.1): the mean of seven 0-10 sub-scores (see verb_score)
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flref_compare as fc  # noqa: E402


def score(r):
    """0-10 sound score (the FL parity tracker's scale, round 170i)."""
    if not r or r.get('missing'):
        return None
    s = r.get('silent')
    if s == 'fl':
        return None
    if s == 'lynx':
        return 0
    e = r['esr_db']
    spec = r.get('spec_db') or 0
    env = r.get('env_corr') or 0
    rms = abs(r.get('rms_db') or 0)
    if e <= -70:
        return 10
    if e <= -55:
        return 9
    if e <= -40:
        return 8
    if spec <= 0.15 and rms <= 0.2 and env >= 0.995:
        return 8 if e <= -20 else 7
    if e <= -25:
        return 7
    if spec <= 0.6 and rms <= 0.5 and env >= 0.98:
        return 6
    if e <= -12:
        return 5
    if spec <= 2.5 and rms <= 1.5:
        return 4
    if spec <= 4 and rms <= 3:
        return 3
    if rms <= 5:
        return 2
    return 1


def read_wav(path):
    """Stereo float64 frames and the rate of a PCM 16 / 24 / 32-bit or float 32 WAV (fxmeasure and fxrender write float)."""
    import struct
    import numpy as np
    d = open(path, 'rb').read()
    i, fmt = 12, None
    while i + 8 <= len(d):
        cid, size = d[i:i + 4], struct.unpack('<I', d[i + 4:i + 8])[0]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', d[i + 8:i + 24])
        elif cid == b'data' and fmt:
            tag, ch, rate, _, _, bits = fmt
            raw = d[i + 8:i + 8 + size]
            if bits == 32 and tag in (3, 0xFFFE):
                x = np.frombuffer(raw[:len(raw) // 4 * 4], '<f4').astype(np.float64)
            elif bits == 16:
                x = np.frombuffer(raw[:len(raw) // 2 * 2], '<i2').astype(np.float64) / 32768.0
            elif bits == 24:
                a = np.frombuffer(raw[:len(raw) // 3 * 3], np.uint8).reshape(-1, 3).astype(np.int32)
                x = ((a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)) << 8 >> 8) / 8388608.0
            else:
                x = np.frombuffer(raw[:len(raw) // 4 * 4], '<i4').astype(np.float64) / 2147483648.0
            x = x[:len(x) // ch * ch].reshape(-1, ch)
            if ch == 1:
                x = np.repeat(x, 2, axis=1)
            return x[:, :2], rate
        i += 8 + size + (size & 1)
    raise ValueError('not a WAV: ' + path)


fc.read_fl = lambda p: read_wav(p)[0]
fc.read_lynx = lambda p: read_wav(p)[0]


def rate_of(path):
    return read_wav(path)[1]


def pair(a, b):
    fc.SR = rate_of(a)
    r = fc.compare(a, b)
    r['score'] = score(r)
    return r


# ------------------------------------------------------------------ reverbs (rounds 172b/c/d share this definition)
OCTAVES = (125, 250, 500, 1000, 2000, 4000, 8000)


def _band(x, lo, hi, sr):
    """Zero-phase FFT band pass with raised-cosine edges a third of an octave wide."""
    import numpy as np
    n = 1 << int(np.ceil(np.log2(len(x))))
    f = np.fft.rfftfreq(n, 1 / sr)
    lf = np.log2(np.maximum(f, 1e-9))
    w = np.clip((lf - np.log2(lo)) * 3 + .5, 0, 1) * np.clip((np.log2(hi) - lf) * 3 + .5, 0, 1)
    return np.fft.irfft(np.fft.rfft(x, n) * w, n)[:len(x)]


def _edc(h):
    import numpy as np
    e = np.cumsum((h ** 2)[::-1])[::-1]
    return 10 * np.log10(np.maximum(e / max(e[0], 1e-30), 1e-30))


def _t60(edc, sr, hi=-5.0, lo=-25.0):
    import numpy as np
    i0, i1 = int(np.argmax(edc <= hi)), int(np.argmax(edc <= lo))
    if i1 <= i0 + 10:
        return None
    k = np.polyfit(np.arange(i0, i1) / sr, edc[i0:i1], 1)[0]
    return float(-60.0 / k) if k < 0 else None


def verb_stats(path, t0=0.1):
    """Black-box reverb measurements of an impulse render (the impulse at t0 s): RT60 per octave (Schroeder EDC,
    T20 x 3), the broadband EDC, pre-delay, early energy (first 80 ms after the first arrival), the tail's
    third-octave balance, the tail's L/R correlation, its spectral-centroid fluctuation (modulation) and the energy."""
    import numpy as np
    x, sr = read_wav(path)
    x = x[int(t0 * sr):]
    m = x.mean(1)
    a = np.abs(m)
    first = int(np.argmax(a > a.max() * 0.05)) if a.max() > 0 else 0
    h, hm = x[first:], m[first:]
    st = {'sr': sr, 'predelay_ms': first / sr * 1000, 'energy_db': 10 * np.log10(np.sum(h ** 2) + 1e-30)}
    st['rt60'] = [_t60(_edc(_band(hm, f / 2 ** .5, min(f * 2 ** .5, sr * 0.475), sr)), sr) for f in OCTAVES]
    st['edc'] = _edc(hm)
    e80 = np.sum(h[:int(0.08 * sr)] ** 2)
    st['early_db'] = 10 * np.log10(e80 / max(np.sum(h ** 2), 1e-30) + 1e-30)
    tail = hm[int(0.1 * sr):]
    sp = np.abs(np.fft.rfft(tail)) ** 2 if len(tail) > 64 else np.zeros(2)
    fr = np.fft.rfftfreq(len(tail), 1 / sr) if len(tail) > 64 else np.zeros(2)
    bands = [np.sum(sp[(fr >= 25 * 2 ** (k / 3)) & (fr < 25 * 2 ** ((k + 1) / 3))]) for k in range(29)]
    b = 10 * np.log10(np.array(bands) + 1e-30)
    st['tail_spec'] = b - b.max()
    l, r = h[int(.05 * sr):int(1.0 * sr), 0], h[int(.05 * sr):int(1.0 * sr), 1]
    st['lr_corr'] = float(np.dot(l, r) / (np.linalg.norm(l) * np.linalg.norm(r) + 1e-30))
    w = int(0.02 * sr)
    seg = tail[:len(tail) // w * w].reshape(-1, w)
    keep = np.sqrt(np.mean(seg ** 2, 1)) > 1e-5
    cents = []
    ff = np.fft.rfftfreq(w, 1 / sr)
    for fr_ in seg[keep][:200]:
        s_ = np.abs(np.fft.rfft(fr_ * np.hanning(w)))
        cents.append(np.sum(s_ * ff) / max(np.sum(s_), 1e-30))
    st['centroid_fluct'] = float(np.std(np.diff(cents)) if len(cents) > 3 else 0.0)
    return st


def verb_score(a, b, t0=0.1):
    """Reverb score 0-10 of LYNX's impulse render `b` against the original's `a`: the mean of seven sub-scores, each
    10 at a perfect match and 0 at the given distance (linear in between):
      rt60     mean |log2 ratio| of the RT60 per octave 125 Hz - 8 kHz      0 at 1 octave
      edc      mean |dB| between the broadband EDCs over 0 … −40 dB of the original, time scaled 0..1   0 at 10 dB
      early    |early-energy dB difference| / 10 + |pre-delay ms difference| / 50 (the larger)          0 at 1
      balance  mean |dB| between the tails' normalised third-octave spectra (bands within 40 dB)      0 at 10 dB
      stereo   |difference of the tails' L/R correlation|                                              0 at 1
      mod      |log ratio| of the tails' spectral-centroid fluctuation (20 ms frames)                  0 at 1
      level    |energy dB difference|                                                                  0 at 10 dB"""
    import numpy as np
    A, B = verb_stats(a, t0), verb_stats(b, t0)
    lin = lambda d, full: max(0.0, 10.0 * (1.0 - d / full))
    pairs = [(x, y) for x, y in zip(A['rt60'], B['rt60']) if x and y]
    d_rt = float(np.mean([abs(np.log2(y / x)) for x, y in pairs])) if pairs else 1.0
    ea, eb = A['edc'], B['edc']
    na = int(np.argmax(ea <= -40)) or len(ea)
    nb = int(np.argmax(eb <= -40)) or len(eb)
    u = np.linspace(0, 1, 200)
    d_edc = float(np.mean(np.abs(np.interp(u * na, np.arange(len(ea)), ea) - np.interp(u * na, np.arange(len(eb)), eb))))
    d_early = max(abs(A['early_db'] - B['early_db']) / 10.0, abs(A['predelay_ms'] - B['predelay_ms']) / 50.0)
    use = A['tail_spec'] > -40
    d_bal = float(np.mean(np.abs(A['tail_spec'][use] - B['tail_spec'][use]))) if use.any() else 10.0
    d_st = abs(A['lr_corr'] - B['lr_corr'])
    d_mod = abs(np.log((B['centroid_fluct'] + 1.0) / (A['centroid_fluct'] + 1.0)))
    d_lvl = abs(A['energy_db'] - B['energy_db'])
    sub = {'rt60': lin(d_rt, 1.0), 'edc': lin(d_edc, 10.0), 'early': lin(d_early, 1.0), 'balance': lin(d_bal, 10.0),
           'stereo': lin(d_st, 1.0), 'mod': lin(d_mod, 1.0), 'level': lin(d_lvl, 10.0)}
    out = {k: round(v, 2) for k, v in sub.items()}
    out['score'] = round(float(np.mean(list(sub.values()))), 2)
    out['rt60_orig'] = [x and round(x, 3) for x in A['rt60']]
    out['rt60_lynx'] = [x and round(x, 3) for x in B['rt60']]
    out['detail'] = {'d_rt60_oct': round(d_rt, 3), 'd_edc_db': round(d_edc, 2), 'd_early_db': round(A['early_db'] - B['early_db'], 2),
                     'd_predelay_ms': round(B['predelay_ms'] - A['predelay_ms'], 2), 'd_balance_db': round(d_bal, 2),
                     'lr': [round(A['lr_corr'], 3), round(B['lr_corr'], 3)],
                     'mod': [round(A['centroid_fluct'], 1), round(B['centroid_fluct'], 1)], 'd_level_db': round(B['energy_db'] - A['energy_db'], 2)}
    return out


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == 'pair':
        print(json.dumps(pair(sys.argv[2], sys.argv[3])))
    elif len(sys.argv) >= 4 and sys.argv[1] == 'verb':
        print(json.dumps(verb_score(sys.argv[2], sys.argv[3], float(sys.argv[4]) if len(sys.argv) > 4 else 0.1)))
    elif len(sys.argv) >= 4 and sys.argv[1] == 'dir':
        for f in sorted(os.listdir(sys.argv[2])):
            b = os.path.join(sys.argv[3], f)
            if f.endswith('.wav') and os.path.exists(b):
                r = pair(os.path.join(sys.argv[2], f), b)
                r['name'] = f[:-4]
                print(json.dumps(r))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
