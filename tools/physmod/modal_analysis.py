#!/usr/bin/env python3
"""modal_analysis.py - shared signal analysis for LYNX Model (round 190ni): the timbre score (timbre_score.py) and the
auto-calibration (calibrate.py) both read notes through these functions. numpy only, so it runs wherever the other
LYNX scoring scripts run (scripts/taillab.py, scripts/fxparity.py).

  read_wav / write_wav    PCM 16 / 24 / 32-bit float WAV, mixed to mono (float64)
  onset                   the first sample of the note (1 ms envelope rises above a fraction of its peak)
  pick_peaks              the prominent spectral peaks of a note (early and main windows, parabolic interpolation)
  track                   one partial's amplitude over time (heterodyne: Hann-windowed DFT at its frequency)
  fit_decay               exponential decay of a partial's track: level at the onset, T60, fit error
  analyse_modes           peaks -> tracked, fitted modes (frequency, initial amplitude, T60), strongest first
  rise_time / centroid    attack measures (10-90 % rise of the 0.5 ms envelope, at least 0.5 ms; spectral centroid)
"""
import struct

import numpy as np


# --- files ----------------------------------------------------------------------------------------------------------

def read_wav(path, mono=True):
    """(samples float64, sample rate). PCM 16 / 24 / 32 bit and IEEE float 32; WAVE_FORMAT_EXTENSIBLE is accepted."""
    d = open(path, 'rb').read()
    if d[:4] != b'RIFF' or d[8:12] != b'WAVE':
        raise ValueError('not a WAV file: %s' % path)
    i, fmt = 12, None
    while i + 8 <= len(d):
        cid, size = d[i:i + 4], struct.unpack('<I', d[i + 4:i + 8])[0]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', d[i + 8:i + 24])
        elif cid == b'data' and fmt:
            tag, ch, rate, _, _, bits = fmt
            raw = d[i + 8:i + 8 + size]
            if bits == 24:
                a = np.frombuffer(raw[:len(raw) // 3 * 3], np.uint8).reshape(-1, 3).astype(np.int32)
                x = ((a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)) << 8 >> 8) / 8388608.0
            elif bits == 16:
                x = np.frombuffer(raw[:len(raw) // 2 * 2], '<i2') / 32768.0
            elif bits == 32 and tag in (3, 0xFFFE):
                x = np.frombuffer(raw[:len(raw) // 4 * 4], '<f4').astype(np.float64)
            elif bits == 32:
                x = np.frombuffer(raw[:len(raw) // 4 * 4], '<i4') / 2147483648.0
            else:
                raise ValueError('unsupported WAV: %d bits' % bits)
            x = x[:len(x) // ch * ch].reshape(-1, ch)
            return (x.mean(axis=1) if mono else x), rate
        i += 8 + size + (size & 1)
    raise ValueError('no data chunk: %s' % path)


def write_wav(path, x, sr, bits=16):
    """mono or (n, 2) float array in -1..1 -> PCM WAV (16 or 24 bit)"""
    y = np.asarray(x, dtype=np.float64)
    ch = 1 if y.ndim == 1 else y.shape[1]
    y = np.clip(y.reshape(-1), -1.0, 1.0)
    if bits == 24:
        v = np.round(y * 8388607.0).astype(np.int32)
        pcm = np.stack([v & 255, (v >> 8) & 255, (v >> 16) & 255], 1).astype(np.uint8).tobytes()
    else:
        bits = 16
        pcm = np.round(y * 32767.0).astype('<i2').tobytes()
    ba = ch * bits // 8
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, ch, sr, sr * ba, ba, bits))
        f.write(b'data' + struct.pack('<I', len(pcm)) + pcm)


# --- time -----------------------------------------------------------------------------------------------------------

def smooth_env(x, sr, ms):
    """|x| smoothed by a moving RMS of `ms` milliseconds (same length as x)"""
    n = max(1, int(sr * ms / 1000.0))
    c = np.cumsum(np.concatenate([[0.0], x * x]))
    e = np.sqrt(np.maximum((c[n:] - c[:-n]) / n, 0.0))
    return np.concatenate([e, np.full(n - 1, e[-1] if len(e) else 0.0)])


def onset(x, sr, fraction=0.05):
    """first sample where the 1 ms envelope reaches `fraction` of its peak, moved back to where it left the floor"""
    e = smooth_env(x, sr, 1.0)
    if e.max() <= 0:
        return 0
    k = int(np.nonzero(e >= fraction * e.max())[0][0])
    floor = max(np.median(e[:max(k, 1)]) * 2.0, e.max() * 1e-3)
    j = k
    while j > 0 and e[j - 1] > floor and k - j < int(0.02 * sr):
        j -= 1
    return max(0, j - int(0.0005 * sr))


def band_env(x, sr, ms=0.5, span=0.15):
    """amplitude envelope of the first `span` s, phase-blind between bands: the root of the summed energies of the
    analytic signals of octave bands (31 Hz ... Nyquist), each smoothed over `ms`. A lone partial gives its exact
    amplitude; partials in different octaves add their energies whatever their phases."""
    n = int(span * sr)
    seg = np.asarray(x[:n], np.float64)
    if len(seg) < 64:
        return np.zeros(len(seg))
    N = 1 << int(np.ceil(np.log2(len(seg) * 2)))
    X = np.fft.fft(seg, N)
    fr = np.abs(np.fft.fftfreq(N, 1.0 / sr))
    pos = np.fft.fftfreq(N) > 0
    e2 = np.zeros(len(seg))
    lo = 0.0
    edges = [44.2 * 2 ** k for k in range(12)] + [sr]
    for hi in edges:
        m = pos & (fr >= lo) & (fr < hi)
        lo = hi
        if not m.any():
            continue
        z = np.fft.ifft(np.where(m, 2.0 * X, 0.0))[:len(seg)]
        e2 += smooth_env(np.abs(z), sr, ms) ** 2
    return np.sqrt(e2)


def rise_time(x, sr, lo=0.1, hi=0.9, ms=0.5, floor_s=0.0005):
    """seconds from `lo` to `hi` of the envelope's first maximum (the attack), broadband envelope smoothed over `ms`
    (band_env would smear a 1 ms onset to ~3 ms: an octave band at 500 Hz cannot resolve it)"""
    return rise_of_env(smooth_env(np.asarray(x[:int(0.15 * sr)], np.float64), sr, ms), sr, lo, hi, floor_s)


def rise_of_env(seg, sr, lo=0.1, hi=0.9, floor_s=0.0005):
    """rise_time on an envelope already computed (the calibration combines envelopes of emulated parts)"""
    if len(seg) == 0 or seg.max() <= 0:
        return 0.0
    # the strike's own peak: the first local maximum reaching half of the 150 ms maximum (a marimba's resonator, a
    # bell's build-up can peak later; that is not the attack)
    # (a maximum over +-2 ms: the beats between partials ripple the envelope faster than that)
    big = seg.max()
    h = max(1, int(0.002 * sr))
    pad = np.concatenate([np.full(h, -1.0), seg, np.full(h, -1.0)])
    wmax = np.lib.stride_tricks.sliding_window_view(pad, 2 * h + 1).max(axis=1)
    loc = np.nonzero((seg >= wmax) & (seg >= 0.5 * big))[0]
    pk = int(loc[0]) if len(loc) else int(np.argmax(seg))
    top = seg[pk]
    a = np.nonzero(seg[:pk + 1] >= lo * top)[0]
    b = np.nonzero(seg[:pk + 1] >= hi * top)[0]
    if len(a) == 0 or len(b) == 0:
        return 0.0
    return max(float(b[0] - a[0]) / sr, floor_s)


def centroid(seg, sr, fmin=40.0):
    """spectral centroid (Hz) of a segment, Hann window, above fmin"""
    if len(seg) < 16:
        return 0.0
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    m = fr >= fmin
    return float((S[m] * fr[m]).sum() / max(S[m].sum(), 1e-30))


# --- frequency ------------------------------------------------------------------------------------------------------

def _spectrum(seg, sr, nfft):
    w = np.hanning(len(seg))
    S = np.abs(np.fft.rfft(seg * w, nfft)) * (2.0 / w.sum())
    return S, np.fft.rfftfreq(nfft, 1.0 / sr)


def _interp(S, fr, k):
    if 0 < k < len(S) - 1:
        a, b, c = np.log(S[k - 1] + 1e-30), np.log(S[k] + 1e-30), np.log(S[k + 1] + 1e-30)
        den = a - 2 * b + c
        p = 0.5 * (a - c) / den if den != 0 else 0.0
        p = float(np.clip(p, -0.5, 0.5))
        return float(fr[k] + p * (fr[1] - fr[0])), float(np.exp(b - 0.25 * (a - c) * p))
    return float(fr[k]), float(S[k])


def pick_peaks(x, sr, fmin=40.0, fmax=None, k=40, prom_db=12.0, floor_db=-75.0, windows=((0.004, 0.08), (0.03, 1.0))):
    """prominent spectral peaks of a note that starts at sample 0: (frequency Hz, peak magnitude) strongest first.
    Two windows (early: the fast high modes; main: resolution for the low ones); a peak must stand `prom_db` over the
    median of its +-1/3 octave and lie within `-floor_db` of the window's maximum; picks closer than 1.5 % merge."""
    fmax = fmax or 0.45 * sr
    out = []
    for wi, (t0, t1) in enumerate(windows):
        a, b = int(t0 * sr), min(len(x), int(t1 * sr))
        if b - a < 256:
            continue
        seg = x[a:b]
        nfft = 1 << int(np.ceil(np.log2(max(len(seg) * 4, 1 << 15))))
        S, fr = _spectrum(seg, sr, nfft)
        band = (fr >= fmin) & (fr <= fmax)
        if not band.any():
            continue
        top = S[band].max()
        # local maxima
        loc = np.nonzero((S[1:-1] > S[:-2]) & (S[1:-1] >= S[2:]))[0] + 1
        loc = loc[band[loc] & (S[loc] > top * 10 ** (floor_db / 20.0))]
        loc = loc[np.argsort(S[loc])[::-1]][:400]
        lS = np.log(S + 1e-30)
        res = sr / float(len(seg))  # (the window's resolution: Hann main lobe +-2 res)
        kept = []
        for j in loc:
            # a Hann sidelobe of a stronger peak nearby (first sidelobe -31 dB, falling 18 dB / octave): not a partial
            if any(abs(fr[j] - fr[q]) < 12 * res and S[j] < S[q] * 10 ** (-(25.0 + 18.0 * np.log2(max(abs(fr[j] - fr[q]) / (2.5 * res), 1.0))) / 20.0)
                   for q in kept):
                continue
            kept.append(j)
            lo = np.searchsorted(fr, fr[j] * 2 ** (-1 / 3)); hi = np.searchsorted(fr, fr[j] * 2 ** (1 / 3))
            med = np.median(lS[lo:hi]) if hi > lo else lS[j]
            if 20.0 / np.log(10) * (lS[j] - med) < prom_db:
                continue
            f, m = _interp(S, fr, j)
            out.append((f, m, wi))
    out.sort(key=lambda p: -p[1])
    merged = []
    for f, m, wi in out:
        if all(abs(f - g) > 0.015 * g for g, _, _ in merged):
            merged.append((f, m, wi))
        if len(merged) >= k:
            break
    return merged


def peak_near(x, sr, f, cents=60.0, window=(0.03, 1.0)):
    """(frequency, magnitude) of the largest spectral peak within +-cents of f in the given window (as pick_peaks
    computes it), or None"""
    a, b = int(window[0] * sr), min(len(x), int(window[1] * sr))
    if b - a < 256:
        return None
    seg = x[a:b]
    nfft = 1 << int(np.ceil(np.log2(max(len(seg) * 4, 1 << 15))))
    S, fr = _spectrum(seg, sr, nfft)
    lo, hi = np.searchsorted(fr, f * 2 ** (-cents / 1200.0)), np.searchsorted(fr, f * 2 ** (cents / 1200.0))
    if hi <= lo + 1:
        return None
    lo, hi = max(lo, 1), min(hi, len(S) - 1)
    seg = S[lo:hi]
    loc = np.nonzero((seg > S[lo - 1:hi - 1]) & (seg >= S[lo + 1:hi + 1]))[0]
    if len(loc) == 0:
        return None  # (no local maximum inside: only a neighbour's skirt)
    j = lo + int(loc[np.argmax(seg[loc])])
    return _interp(S, fr, j)


def track(x, sr, f, win, hop):
    """(times, amplitudes): the partial at f Hz, Hann-windowed DFT per frame (amplitude of a unit sine = 1)"""
    win = int(min(win, len(x)))
    if win < 16:
        return np.zeros(0), np.zeros(0)
    w = np.hanning(win)
    g = 2.0 / w.sum()
    c = np.exp(-2j * np.pi * f * np.arange(win) / sr) * w * g
    starts = np.arange(0, len(x) - win + 1, hop)
    fr = np.lib.stride_tricks.sliding_window_view(x, win)[starts]
    z = fr @ c
    return (starts + win / 2.0) / sr, np.abs(z)


def track_window(f, neighbours, sr):
    """window length for tracking f: resolves its nearest neighbour (3 main-lobe widths) and holds >= 6 periods"""
    sp = min([abs(f - g) for g in neighbours if abs(f - g) > 1e-6] or [f])
    return int(np.clip(max(3.0 * 2.0 * sr / max(sp, 1.0), 6.0 * sr / f), 256, 16384))


def window_gain(t60, win, sr):
    """what a Hann window of `win` samples reads of a decaying partial re its value at the window's centre (> 1: the
    window's earlier half weighs the louder past); 1 for a slow decay"""
    w = np.hanning(int(win))
    n = (np.arange(len(w)) - (len(w) - 1) / 2.0) / sr
    return float((w * np.exp(-6.9078 / max(t60, 1e-3) * n)).sum() / w.sum())


def fit_decay(t, a, range_db=45.0, floor_margin_db=8.0, min_points=5, win=None, sr=None):
    """exponential decay fitted in dB from the track's peak: {'a0' level at t=0, 't60' s, 'rms_db', 't_peak'} or None.
    Uses the points between the peak and the first fall to (noise floor + margin) or (peak - range_db).
    (round 190nm) With the track's window `win` (samples) and rate `sr`: a0 is corrected for what the window reads of a
    fast decay (window_gain; a 0.15 s partial tracked with a 0.27 s window read 37 dB too loud). The timbre score
    calls it without, as before."""
    if len(a) < min_points:
        return None
    adb = 20.0 * np.log10(a + 1e-12)
    floor = np.percentile(adb[-max(5, len(adb) // 8):], 50)
    pk = int(np.argmax(adb))
    stop = max(floor + floor_margin_db, adb[pk] - range_db)
    below = np.nonzero(adb[pk:] < stop)[0]
    end = pk + (int(below[0]) if len(below) else len(adb) - pk)
    idx = np.arange(pk, end)
    if len(idx) < min_points:
        # a fast mode: fit what there is down to 3 points
        idx = np.arange(pk, min(len(adb), pk + min_points))
        if adb[pk] - floor < 10:
            return None
    tt, yy = t[idx], adb[idx]
    A = np.vstack([np.ones(len(tt)), tt]).T
    sol, *_ = np.linalg.lstsq(A, yy, rcond=None)
    b0, slope = float(sol[0]), float(sol[1])
    if slope >= -1e-3:  # not decaying within the take: a very long T60
        slope = -60.0 / 120.0
    t60 = -60.0 / slope
    rms_db = float(np.sqrt(np.mean((A @ sol - yy) ** 2)))
    # the level at t = 0: the fit at the peak, carried back over the first frame's offset only (a track that peaks
    # late - noise, a neighbour's leakage - must not be extrapolated over its whole rise)
    a0db = b0 + slope * t[pk] - slope * t[0]
    if win is not None and sr:
        a0db -= 20.0 * np.log10(window_gain(t60, win, sr))
    return {'a0': float(10 ** (min(a0db, b0) / 20.0)), 't60': float(t60), 'rms_db': rms_db, 't_peak': float(t[pk]),
            'peak_db': float(adb[pk]), 'floor_db': float(floor), 'points': int(len(idx))}


WINDOWS = ((0.004, 0.08), (0.03, 1.0))


def analyse_modes(x, sr, fmin=40.0, fmax=None, k=40, prom_db=12.0, hop_ms=5.0, peaks=None, window_exact=False):
    """the note's modes, strongest (energy a0^2 * t60) first: [{'f', 'a0', 't60', 'rms_db', 'energy', 'mag'}]
    (window_exact, round 190nm: a0 corrected for the tracking window, fit_decay)"""
    peaks = peaks if peaks is not None else pick_peaks(x, sr, fmin, fmax, k, prom_db)
    freqs = [p[0] for p in peaks]
    hop = max(1, int(sr * hop_ms / 1000.0))
    modes = []
    for f, mag, wi in peaks:
        win = track_window(f, freqs, sr)
        t, a = track(x, sr, f, win, hop)
        p = fit_decay(t, a, win=win if window_exact else None, sr=sr)
        if p is None:
            continue
        if window_exact and p['t60'] < 2.0 * win / sr:
            # (round 190nm: a partial that dies within its tracking window - a long window chosen to resolve a weak
            # neighbour - is tracked again with a window of half its T60: a plucked tine's fast fundamental read
            # through a 0.3 s window lost its first 20 dB and seemed to die four times too fast)
            win2 = int(np.clip(0.5 * p['t60'] * sr, 256, win))
            if win2 < win:
                t2, a2 = track(x, sr, f, win2, hop)
                p2 = fit_decay(t2, a2, win=win2, sr=sr)
                if p2 is not None:
                    p, win = p2, win2
        p['f'] = float(f)
        p['mag'] = float(mag)
        p['win'] = win
        p['window'] = wi
        p['energy'] = p['a0'] ** 2 * p['t60']
        modes.append(p)
    modes.sort(key=lambda m: -m['energy'])
    return modes


def third_octave_centres(lo=31.25, hi=16000.0):
    c = 1000.0 * 2.0 ** (np.arange(-15, 13) / 3.0)
    return c[(c >= lo * 0.99) & (c <= hi * 1.01)]


def band_energy_grid(x, sr, edges_s, centres=None):
    """energy (dB) per third-octave band (rows) per time slice [edges_s[i], edges_s[i+1]) (columns)"""
    centres = third_octave_centres() if centres is None else centres
    out = np.full((len(centres), len(edges_s) - 1), -200.0)
    for j in range(len(edges_s) - 1):
        a, b = int(edges_s[j] * sr), int(edges_s[j + 1] * sr)
        seg = x[a:min(b, len(x))]
        if len(seg) < 32:
            continue
        nfft = 1 << int(np.ceil(np.log2(max(len(seg), 4096))))
        S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft)) ** 2
        fr = np.fft.rfftfreq(nfft, 1.0 / sr)
        for i, c in enumerate(centres):
            m = (fr >= c * 2 ** (-1 / 6)) & (fr < c * 2 ** (1 / 6))
            out[i, j] = 10.0 * np.log10(S[m].sum() / len(seg) + 1e-30) if m.any() else -200.0
    return out


# --- round 190nm: two components per partial (a two-stage decay, a beating pair) ---------------------------------------

def track_complex(x, sr, f, win, hop):
    """(times, complex baseband track): the partial at f Hz as track() measures it, heterodyned to 0 Hz (a component
    at f + d Hz turns at d Hz; the amplitude of a unit sine = 1)"""
    win = int(min(win, len(x)))
    if win < 16:
        return np.zeros(0), np.zeros(0, complex)
    w = np.hanning(win)
    g = 2.0 / w.sum()
    c = np.exp(-2j * np.pi * f * np.arange(win) / sr) * w * g
    starts = np.arange(0, len(x) - win + 1, hop)
    fr = np.lib.stride_tricks.sliding_window_view(x, win)[starts]
    z = (fr @ c) * np.exp(-2j * np.pi * f * starts / sr)
    return (starts + win / 2.0) / sr, z


def _pencil(z, order):
    """the poles of `order` damped complex exponentials in z (matrix pencil, pencil parameter len / 3)"""
    n = len(z)
    L = max(order, n // 3)
    Y = np.lib.stride_tricks.sliding_window_view(z, L + 1)
    U, s, Vh = np.linalg.svd(Y, full_matrices=False)
    V = Vh[:order].T  # (the row space of Y: spanned by [1, p, p^2, ...] per pole)
    V1, V2 = V[:-1], V[1:]
    return np.linalg.eigvals(np.linalg.pinv(V1) @ V2)


def fit_two(t, z, single, max_split_hz=3.0, min_gain_db=0.6, min_level_db=-30.0):
    """a partial's track as TWO components (round 190nm): c (e^(l1 t) + rho e^(l2 t)), rho real (the strike drives both
    components of a doublet - a near-degenerate pair of a tube or a tine - from rest, so they start in phase or in
    antiphase); l = -alpha + i 2 pi d. Same d: a two-stage decay (a fast and a slow part); different d: a beating pair.
    Fitted on the same span as fit_decay's single exponential (`single`, its result), by the matrix pencil on the complex
    track, then rho by a search; kept only when the track's dB error falls by `min_gain_db` and both components matter.
    Returns None or {'parts': [{'df' Hz re the tracked frequency, 'a0', 't60'}, ...], 'rms_db', 'rms_db_single'}"""
    if single is None or len(t) < 12:
        return None
    adb = 20 * np.log10(np.abs(z) + 1e-12)
    pk = int(np.argmax(adb))
    stop = max(single['floor_db'] + 8.0, adb[pk] - 45.0)
    below = np.nonzero(adb[pk:] < stop)[0]
    end = pk + (int(below[0]) if len(below) else len(adb) - pk)
    if end - pk < 12:
        return None
    tt, zz = t[pk:end], z[pk:end]
    dt = tt[1] - tt[0]
    # the single exponential's error on this span (a straight line in dB)
    A1 = np.vstack([np.ones(len(tt)), tt]).T
    sol, *_ = np.linalg.lstsq(A1, adb[pk:end], rcond=None)
    rms1 = float(np.sqrt(np.mean((A1 @ sol - adb[pk:end]) ** 2)))
    try:
        poles = _pencil(zz, 2)
    except np.linalg.LinAlgError:
        return None
    lam = np.log(poles.astype(complex) + 1e-30) / dt
    alpha = -lam.real
    df = lam.imag / (2 * np.pi)
    if np.any(alpha <= 0.0) or np.any(np.abs(df) > max_split_hz) or not np.all(np.isfinite(lam)):
        return None
    E = np.exp(np.outer(tt - tt[0], lam))

    def trial(rho):
        b = E[:, 0] + rho * E[:, 1]
        c = (b.conj() @ zz) / max((b.conj() @ b).real, 1e-30)
        m = 20 * np.log10(np.abs(c * b) + 1e-12)
        return float(np.sqrt(np.mean(np.minimum(np.abs(m - adb[pk:end]), 30.0) ** 2))), rho, c

    # (rho: the free complex amplitudes' ratio projected onto the real axis, and a search over signed ratios)
    cc, *_ = np.linalg.lstsq(E, zz, rcond=None)
    q = cc[1] / cc[0] if abs(cc[0]) > 0 else 1.0
    proj = abs(q) * (1.0 if q.real >= 0 else -1.0)
    grid = np.concatenate([-np.geomspace(30.0, 0.03, 40), np.geomspace(0.03, 30.0, 40), [proj] if np.isfinite(proj) and proj != 0 else []])
    best = min((trial(r) for r in grid), key=lambda q: q[0])
    best = min([best] + [trial(best[1] * k) for k in np.geomspace(0.84, 1.19, 15)], key=lambda q: q[0])
    err, rho, c = best
    if err > rms1 - min_gain_db:
        return None
    # the components' levels at the span's start, carried back over the first frame's offset only (as fit_decay does)
    a = np.abs(c) * np.array([1.0, abs(rho)]) * np.exp(alpha * t[0])
    lv = 20 * np.log10(a / a.max())
    if lv.min() < min_level_db:
        return None
    sign = [1.0, 1.0 if rho > 0 else -1.0]
    parts = [{'df': float(df[k]), 'a0': float(a[k]), 't60': float(min(6.9078 / alpha[k], 120.0)), 'sign': sign[k]} for k in range(2)]
    return {'parts': parts, 'rms_db': round(err, 3), 'rms_db_single': round(rms1, 3)}
