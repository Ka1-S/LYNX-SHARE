#!/usr/bin/env python3
# LYNX vocal lab (round 190li): synthetic sung phrases with a known ground truth, for scoring monophonic pitch editors
# (LYNX Newtone, Melodyne as the reference bar) without any third-party recording.
#
# The voice is a source-filter model rendered additively: every harmonic k of the pitch curve f0(t) gets the amplitude
# of a fixed spectral envelope at k*f0(t) (glottal tilt x vowel formant resonances), so the envelope is exactly known
# and a "perfect" formant-preserving pitch edit is simply the same phrase rendered from an edited score. Consonants
# (s, sh, t, k, h) are shaped noise bursts placed between the vowels; the edits never touch them, so in a perfect edit
# they are bit-for-bit the source's. Jitter, shimmer and a little aspiration noise make it less sterile.
#
# usage: vocalsynth.py <out dir>            writes every phrase and its edit "ideals" (+ .json ground truth)
#        (library use: from vocalsynth import Phrase, render, PHRASES, EDITS)
import json, math, pathlib, sys, wave
import numpy as np

SR = 48000

# vowel formants (F, bandwidth) Hz, after Peterson & Barney (1952) / Hillenbrand (1995) averages
VOWELS = {
    'female': {'a': [(850, 90), (1220, 110), (2810, 160), (3900, 250), (4950, 300)],
               'e': [(560, 70), (2320, 120), (2950, 170), (3900, 250), (4950, 300)],
               'i': [(330, 60), (2750, 120), (3300, 180), (4150, 250), (4950, 300)],
               'o': [(560, 70), (920, 90), (2830, 160), (3900, 250), (4950, 300)],
               'u': [(370, 60), (950, 90), (2670, 160), (3900, 250), (4950, 300)]},
    'male': {'a': [(730, 80), (1090, 100), (2440, 140), (3400, 220), (4500, 280)],
             'e': [(530, 60), (1840, 110), (2480, 150), (3400, 220), (4500, 280)],
             'i': [(270, 50), (2290, 110), (3010, 160), (3400, 220), (4500, 280)],
             'o': [(570, 60), (840, 80), (2410, 140), (3400, 220), (4500, 280)],
             'u': [(300, 50), (870, 80), (2240, 140), (3400, 220), (4500, 280)]},
    # a reed / bowed instrument body: fixed resonances (no vowels), brighter tilt
    'instrument': {'x': [(520, 120), (1350, 200), (2500, 300), (3600, 400), (5200, 600)]},
}
TILT_HZ = {'female': 450.0, 'male': 300.0, 'instrument': 900.0}


def envelope_db(freqs, formants, tilt_hz):
    """The source-filter spectral envelope (dB) at `freqs` (array): glottal/radiation tilt x formant cascade."""
    f = np.asarray(freqs, dtype=np.float64)
    h = 1.0 / (1.0 + f / tilt_hz)                         # ~ -6 dB/oct above the tilt corner
    for F, B in formants:
        h = h * (F * F) / np.sqrt((F * F - f * f) ** 2 + (B * f) ** 2 + 1e-9)
    return 20 * np.log10(h + 1e-12)


def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)


class Note:
    """One sung note. midi = the intended semitone; detune / drift / vibrato / scoop in cents."""
    def __init__(self, midi, dur, vowel='a', detune=0.0, vib=0.0, vib_rate=5.5, vib_delay=0.25, drift=0.0, scoop=0.0,
                 before=None, before_dur=0.0, legato=False, gap=0.0, gain_db=0.0):
        self.midi, self.dur, self.vowel = midi, dur, vowel
        self.detune, self.vib, self.vib_rate, self.vib_delay = detune, vib, vib_rate, vib_delay
        self.drift, self.scoop = drift, scoop
        self.before, self.before_dur = before, before_dur   # consonant before the vowel ('s', 'sh', 't', 'k', 'h')
        self.legato = legato                                 # sung on from the note before (a glide, no break)
        self.gap = gap                                       # silence before (s), ahead of the consonant
        self.gain_db = gain_db
        # edit fields (the ideal renders)
        self.shift = 0.0        # semitones
        self.centre = 0.0       # 0..1: detune removed (pitch centre correction)
        self.mod_scale = 1.0    # vibrato depth factor (Melodyne's Pitch Modulation)
        self.drift_scale = 1.0  # slow drift factor (Melodyne's Pitch Drift)
        self.formant_st = 0.0   # formant shift (semitones)
        self.time_scale = 1.0   # note length factor
        self.offset = 0.0       # start moved (s)
        self.level = 1.0        # the voiced part's gain (ideals: the source note's RMS kept through a pitch / formant move)

    def copy(self):
        n = Note(self.midi, self.dur)
        n.__dict__.update(self.__dict__)
        return n


class Phrase:
    def __init__(self, name, voice, notes, seed=1, tail=0.3, lead=0.25):
        self.name, self.voice, self.notes, self.seed, self.tail, self.lead = name, voice, notes, seed, tail, lead

    def copy(self):
        return Phrase(self.name, self.voice, [n.copy() for n in self.notes], self.seed, self.tail, self.lead)


def shaped_noise(n, rng, lo, hi, tilt=0.0):
    """White noise band-shaped in the FFT domain: a smooth band lo..hi Hz (+ tilt dB/oct above lo)."""
    if n <= 0:
        return np.zeros(0)
    w = rng.standard_normal(n)
    S = np.fft.rfft(w)
    f = np.fft.rfftfreq(n, 1 / SR)
    g = 1 / (1 + (lo / np.maximum(f, 1)) ** 4) / (1 + (f / hi) ** 4)
    if tilt:
        g = g * (np.maximum(f, lo) / lo) ** (tilt / 6.02)
    y = np.fft.irfft(S * g, n)
    return y / (np.sqrt(np.mean(y ** 2)) + 1e-12)


def consonant(kind, dur, rng, vowel_formants, tilt_hz):
    n = int(dur * SR)
    t = np.arange(n) / SR
    if kind == 's':
        y = shaped_noise(n, rng, 4200, 9500) * 0.07
        env = np.minimum(1, np.minimum(t / 0.03, (dur - t) / 0.025))
    elif kind == 'sh':
        y = shaped_noise(n, rng, 2000, 6000) * 0.07
        env = np.minimum(1, np.minimum(t / 0.03, (dur - t) / 0.025))
    elif kind == 'h':
        # breath: noise through the next vowel's envelope, weak
        w = rng.standard_normal(n)
        S = np.fft.rfft(w)
        f = np.fft.rfftfreq(n, 1 / SR)
        g = 10 ** (envelope_db(f, vowel_formants, tilt_hz * 4) / 20)
        y = np.fft.irfft(S * g, n)
        y = y / (np.sqrt(np.mean(y ** 2)) + 1e-12) * 0.025
        env = np.minimum(1, np.minimum(t / 0.06, (dur - t) / 0.03))
    elif kind in ('t', 'k'):
        # closure (silence), a burst and a short aspiration
        y = np.zeros(n)
        b0 = int(0.4 * n)
        lo, hi = (2500, 9000) if kind == 't' else (1200, 4000)
        bl = n - b0
        burst = shaped_noise(bl, rng, lo, hi)
        tb = np.arange(bl) / SR
        y[b0:] = burst * (0.25 * np.exp(-tb / 0.004) + 0.03 * np.exp(-tb / 0.03))
        env = np.ones(n)
    else:
        raise ValueError(kind)
    return y * env


def build(phrase):
    """Timeline of the phrase: per-sample f0 (0 = unvoiced), vowel formant track, amplitude, consonant segments."""
    segs = []   # (kind, start, end, note index or -1)
    t = phrase.lead
    notes_out = []
    for i, nt in enumerate(phrase.notes):
        t += nt.gap
        if nt.before:
            segs.append((nt.before, t, t + nt.before_dur, i))
            t += nt.before_dur
        start = t + nt.offset
        dur = nt.dur * nt.time_scale
        notes_out.append((start, start + dur))
        t = start + dur
    total = t + phrase.tail
    n = int(total * SR)
    return segs, notes_out, n


def render(phrase, want_truth=False):
    rng = np.random.default_rng(phrase.seed)
    segs, spans, n = build(phrase)
    voice = phrase.voice
    tilt = TILT_HZ[voice]
    cents = np.full(n, np.nan)       # the pitch curve (MIDI cents) where voiced
    amp = np.zeros(n)
    fshift = np.zeros(n)             # formant shift (semitones) per sample
    # formants per sample: interpolated between vowels
    F = np.zeros((n, 5)); B = np.zeros((n, 5))
    notes = phrase.notes
    tt = np.arange(n) / SR
    for i, (nt, (a, b)) in enumerate(zip(notes, spans)):
        ia, ib = int(a * SR), int(b * SR)
        u = (np.arange(ia, ib) - ia) / SR
        dur = (ib - ia) / SR
        # drift: a slow slide from +drift to -drift cents (zero mean); vibrato after its delay with a 150 ms fade-in
        base = (nt.midi + nt.shift) * 100 + nt.detune * (1 - nt.centre)
        drift = nt.drift * nt.drift_scale * np.cos(np.pi * np.clip(u / max(dur, 1e-3), 0, 1))   # +d .. -d, zero mean
        vdepth = nt.vib * nt.mod_scale * np.clip((u - nt.vib_delay) / 0.15, 0, 1)
        # vibrato phase continuous in time (time edits keep the natural rate)
        vib = vdepth * np.sin(2 * np.pi * nt.vib_rate * u)
        scoop = nt.scoop * np.exp(-u / 0.035)   # a scoop into the note (its first ~80 ms)
        c = base + drift + vib + scoop
        cents[ia:ib] = c
        # amplitude: attack / release, legato notes join without a dip
        att = 0.012 if nt.legato else 0.04
        rel = 0.012 if (i + 1 < len(notes) and notes[i + 1].legato) else 0.07
        e = np.minimum(1, np.minimum(u / att, (dur - u) / rel)) * 10 ** ((nt.gain_db) / 20) * nt.level
        amp[ia:ib] = np.maximum(amp[ia:ib], e)
        fm = VOWELS[voice].get(nt.vowel) or next(iter(VOWELS[voice].values()))
        F[ia:ib] = [x[0] for x in fm]
        B[ia:ib] = [x[1] for x in fm]
        fshift[ia:ib] = nt.formant_st
    # legato glides: a raised-cosine (in cents) over 70 ms centred on the boundary between two legato notes
    for i in range(1, len(notes)):
        if not notes[i].legato:
            continue
        a0, b0 = spans[i - 1]; a1, b1 = spans[i]
        if abs(b0 - a1) > 1e-6:
            continue
        g = 0.035
        ia, ib = int((a1 - g) * SR), int((a1 + g) * SR)
        if ia < int(a0 * SR) or ib > int(b1 * SR):
            continue
        c0, c1 = cents[ia - 1], cents[ib]
        w = 0.5 - 0.5 * np.cos(np.pi * (np.arange(ia, ib) - ia) / (ib - ia))
        cents[ia:ib] = c0 + (c1 - c0) * w
        # formants glide over the same span
        F[ia:ib] = F[ia - 1] + (F[ib] - F[ia - 1]) * w[:, None]
        B[ia:ib] = B[ia - 1] + (B[ib] - B[ia - 1]) * w[:, None]
    voiced = ~np.isnan(cents)
    # jitter: a little band-limited pitch noise (~4 cents) and shimmer (~0.3 dB)
    jit = np.convolve(rng.standard_normal(n), np.ones(480) / 480, 'same') * 4 * math.sqrt(480)
    shim = np.convolve(rng.standard_normal(n), np.ones(960) / 960, 'same') * 0.3 * math.sqrt(960)
    cv = np.where(voiced, cents, 6000.0)
    f0 = 440.0 * 2 ** ((cv / 100 + jit / 100 - 69) / 12)
    phase = 2 * np.pi * np.cumsum(np.where(voiced, f0, 0.0)) / SR
    y = np.zeros(n)
    idx = np.where(voiced)[0]
    if len(idx):
        lo, hi = idx[0], idx[-1] + 1
        fl = f0[lo:hi]; ph = phase[lo:hi]; Fv = F[lo:hi] * (2 ** (fshift[lo:hi] / 12))[:, None]; Bv = B[lo:hi]
        av = amp[lo:hi] * 10 ** (shim[lo:hi] / 20)
        acc = np.zeros(hi - lo)
        kmax = int(20000 / max(50.0, fl[voiced[lo:hi]].min())) + 1
        tilt_v = tilt
        for k in range(1, kmax + 1):
            fk = k * fl
            if fk.min() > 20000:
                break
            h = 1.0 / (1.0 + fk / tilt_v)
            for j in range(5):
                Fj, Bj = Fv[:, j], Bv[:, j]
                h = h * (Fj * Fj) / np.sqrt((Fj * Fj - fk * fk) ** 2 + (Bj * fk) ** 2 + 1e-9)
            h = h * np.clip((19000 - fk) / 2000, 0, 1)   # fade out under Nyquist
            acc += h * np.sin(k * ph)
        y[lo:hi] = acc * av * 0.05
        # aspiration noise in the voiced part: the vowel envelope's high part, -30 dB
        asp = shaped_noise(hi - lo, rng, 1800, 9000) * 0.0025 * amp[lo:hi]
        y[lo:hi] += asp
    labels = []
    for kind, a, b, i in segs:
        fm = VOWELS[voice].get(notes[i].vowel) or next(iter(VOWELS[voice].values()))
        c = consonant(kind, b - a, np.random.default_rng(phrase.seed * 1000 + i), fm, tilt)
        ia = int(a * SR)
        y[ia:ia + len(c)] += c
        labels.append({'kind': kind, 'start': a, 'end': b, 'note': i})
    if not want_truth:
        return y
    hop = 240   # 5 ms truth frames
    fr = np.arange(0, n, hop)
    truth = {
        'sr': SR, 'voice': voice, 'tilt_hz': tilt, 'hop': hop,
        'notes': [{'start': a, 'end': b, 'midi': nt.midi + nt.shift, 'cents': (nt.midi + nt.shift) * 100 + nt.detune * (1 - nt.centre),
                   'vowel': nt.vowel, 'legato': nt.legato, 'vib': nt.vib * nt.mod_scale, 'drift': nt.drift * nt.drift_scale,
                   'gain_db': nt.gain_db, 'formant_st': nt.formant_st} for nt, (a, b) in zip(notes, spans)],
        'unvoiced': labels,
        'f0_cents': [None if np.isnan(cents[i]) else round(float(cents[i]), 2) for i in fr],
        'formants': [[round(float(x), 1) for x in (F[i] * 2 ** (fshift[i] / 12))] if voiced[i] else None for i in fr],
        'bandwidths': [[round(float(x), 1) for x in B[i]] if voiced[i] else None for i in fr],
    }
    return y, truth


def write_wav(path, x):
    x = np.clip(x, -1, 1)
    pcm = np.round(x * 8388607).astype('<i4')
    b = pcm.view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(3); w.setframerate(SR); w.writeframes(b)


# ---------------------------------------------------------------------------------------------------- the material
def N(*a, **k):
    return Note(*a, **k)


PHRASES = {
    # female pop-style line: consonants, scoops, detuned notes, vibrato on the long ones, a close-note pair (G4 / F#4+45)
    'f-phrase': Phrase('f-phrase', 'female', [
        N(62, 0.55, 'a', detune=+25, scoop=-90, drift=+10, before='s', before_dur=0.14),
        N(64, 0.42, 'e', detune=-30, drift=-15, legato=True),
        N(65, 0.40, 'i', detune=+15, before='t', before_dur=0.07),
        N(67, 1.20, 'o', detune=-20, vib=45, vib_rate=5.6, vib_delay=0.30, drift=-25, before='h', before_dur=0.16),
        N(66, 0.50, 'o', detune=+40, legato=True),
        N(64, 0.50, 'e', detune=-12, before='s', before_dur=0.12, gap=0.15),
        N(62, 1.00, 'a', detune=+20, vib=35, vib_rate=5.2, vib_delay=0.35, drift=+20, before='k', before_dur=0.08),
    ], seed=11),
    # male line an octave lower, other vowels and detunes, a sh and a breath
    'm-phrase': Phrase('m-phrase', 'male', [
        N(50, 0.60, 'o', detune=-20, scoop=-120, before='h', before_dur=0.15),
        N(52, 0.45, 'a', detune=+30, legato=True),
        N(53, 0.50, 'e', detune=-25, before='sh', before_dur=0.13),
        N(55, 1.30, 'a', detune=+15, vib=40, vib_rate=5.4, vib_delay=0.35, drift=+30, before='t', before_dur=0.07),
        N(53, 0.45, 'i', detune=-35, before='s', before_dur=0.12, gap=0.12),
        N(50, 1.10, 'u', detune=+10, vib=30, vib_rate=5.0, vib_delay=0.40, drift=-20, before='k', before_dur=0.08),
    ], seed=23),
    # a legato instrumental line (sax / cello-like body), glides and vibrato, no consonants
    'i-legato': Phrase('i-legato', 'instrument', [
        N(57, 0.70, 'x', detune=+15, scoop=-60, vib=20, vib_delay=0.30),
        N(59, 0.45, 'x', detune=-20, legato=True),
        N(60, 0.45, 'x', detune=+25, legato=True),
        N(64, 1.10, 'x', detune=-10, vib=30, vib_rate=5.8, vib_delay=0.30, drift=+15, legato=True),
        N(62, 0.45, 'x', detune=+30, legato=True),
        N(60, 1.00, 'x', detune=-15, vib=25, vib_rate=5.5, vib_delay=0.35, drift=-20, legato=True),
    ], seed=37),
}


def _all(p, f):
    for n in p.notes:
        f(n)
    return p


def _one(p, i, f):
    f(p.notes[i])
    return p


def _set(**kw):
    def f(n):
        for k, v in kw.items():
            setattr(n, k, v)
    return f


# the edits: name -> (what it does in words, phrase -> edited phrase). The same list drives the Melodyne capture plan
# (tools/vocallab/README.md) and LYNX's lab renders (newtonelab specs in cases.json).
EDITS = {
    'up3': ('all notes +3 st', lambda p: _all(p, _set(shift=3))),
    'down3': ('all notes -3 st', lambda p: _all(p, _set(shift=-3))),
    'up7': ('all notes +7 st', lambda p: _all(p, _set(shift=7))),
    'down7': ('all notes -7 st', lambda p: _all(p, _set(shift=-7))),
    'one-up3': ('the 4th note +3 st (transitions to its neighbours)', lambda p: _one(p, 3, _set(shift=3))),
    'centre50': ('pitch centre corrected 50 % (drift, vibrato kept)', lambda p: _all(p, _set(centre=0.5))),
    'centre100': ('pitch centre corrected 100 % (drift, vibrato kept)', lambda p: _all(p, _set(centre=1.0))),
    'drift0': ('pitch drift removed (centre, vibrato kept)', lambda p: _all(p, _set(drift_scale=0.0))),
    'correct100': ('centre and drift corrected 100 % (Correct Pitch 100/100)', lambda p: _all(p, _set(centre=1.0, drift_scale=0.0))),
    'mod0': ('vibrato (pitch modulation) 0 %', lambda p: _all(p, _set(mod_scale=0.0))),
    'mod150': ('vibrato (pitch modulation) 150 %', lambda p: _all(p, _set(mod_scale=1.5))),
    'formant+3': ('formants +3 st, pitch kept', lambda p: _all(p, _set(formant_st=3.0))),
    'gain-6': ('the 4th note -6 dB', lambda p: _one(p, 3, lambda n: setattr(n, 'gain_db', n.gain_db - 6))),
    'stretch125': ('the 4th note 25 % longer (time)', lambda p: _one(p, 3, _set(time_scale=1.25))),
}


def ti_spans(t):
    return [(n['start'], n['end']) for n in t['notes']]


def main():
    out = pathlib.Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    for name, ph in PHRASES.items():
        y, truth = render(ph.copy(), True)
        peak = np.max(np.abs(y)); scale = 0.5 / peak
        truth['scale'] = scale
        write_wav(out / f'{name}.wav', y * scale)
        (out / f'{name}.json').write_text(json.dumps(truth))
        for e, (desc, f) in EDITS.items():
            if name == 'i-legato' and e in ('formant+3',):
                continue
            q = f(ph.copy())
            yi, ti = render(q, True)
            if any(a.shift != 0 or a.formant_st != 0 for a in q.notes):
                # a voice moved in pitch (or formants) keeps its loudness: each note's level matched to the source's
                # (the additive model's power goes with 1/f0; a pulse train's with f0; an editor keeps the RMS)
                _, sp, _ = build(ph)
                for i, (a, b) in enumerate(ti_spans(ti)):
                    s0, s1 = int((sp[i][0] + 0.05) * SR), int((sp[i][1] - 0.05) * SR)
                    i0, i1 = int((a + 0.05) * SR), int((b - 0.05) * SR)
                    if s1 - s0 > 960 and i1 - i0 > 960:
                        q.notes[i].level = math.sqrt(np.mean(y[s0:s1] ** 2) / max(1e-20, np.mean(yi[i0:i1] ** 2)))
                yi, ti = render(q, True)
            ti['edit'] = e; ti['desc'] = desc; ti['scale'] = scale
            write_wav(out / f'{name}.ideal.{e}.wav', yi * scale)
            (out / f'{name}.ideal.{e}.json').write_text(json.dumps(ti))
        print(name, f'{len(y)/SR:.2f} s, peak scale {scale:.3f}')


if __name__ == '__main__':
    main()
