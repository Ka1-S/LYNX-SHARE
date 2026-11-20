#!/usr/bin/env python3
"""calibrate.py - LYNX Model's auto-calibration (round 190ni): fits each mallet instrument's mode tables per key and
velocity layer from CC0 recordings and writes the parameter table (.lpm, tools/physmod/lpm.py), its provenance record
and the C++ include LYNX compiles in. Only fitted parameters ship (a few KB per instrument), never audio, and the
attack is synthesised by the engine (a Gaussian contact pulse + band-pass noise), never a recorded transient.

  calibrate.py fit <instrument|all> <notes-root> <manifest.json> <out-dir> [--holdout N] [--inc-dir DIR]
        <notes-root>/<set>/*.wav as fetched with the manifest (path, git blob SHA-1, SHA-256, commit, licence per file).
        --holdout N: leave out every N-th note (by key, starting with the 2nd) and list them in the report: the held-out
        notes test the key interpolation; the shipped tables are fitted on every note (no --holdout).
        --fold F (round 190nl): start with the (2 + F)-th key instead, F < N; the table is <instrument>-holdoutNfF
        (fold 0 keeps the name <instrument>-holdoutN). N folds together hold every inner key out once.
        --inc-dir: also write <instrument>.inc there (tools/make_model_inc.py's format) for src/core/physmod/tables/.
        --set name=value[,...] (round 190nm): override an instrument's settings (INSTRUMENTS) for a variant
        --report-dir: the fit reports (<instrument>.fit.json, per note: modes, tuning, contact, noise, fit losses) go
        there instead of <out-dir> (the shipped run: tables and provenance in resources/physmod/, reports elsewhere).

Per recorded note (one velocity layer):
  1. onset, mono, the sounding key = the file's note label + 12 (VCSL labels sit one octave below the sounding pitch;
     checked against the strongest mode for every file, see the report's `label_octave_check`)
  2. modes: prominent peaks (modal_analysis.pick_peaks), each tracked and fitted with an exponential decay (a0, T60);
     modes below the instrument's lowest useful frequency (room rumble), 60 dB under the strongest, and (kalimba) the
     other tines' sympathetic lines (the same absolute frequency in >= 3 other notes, off the note's own series) dropped;
     the strongest M by energy kept, then (round 190nl) up to `long` more by audible duration, at most 44 per anchor
  3. tuning: the main mode (the strongest within 70 cents of the label) defines the note's tuning offset, removed so
     the instrument plays in tune; mode ratios refer to the key's equal-tempered frequency
  4. strike position law: each mode's bending-mode number (shape) from the instrument's mode series
  5. strike: the noise's band guessed from the first 100 ms with every mode notched out; then the contact time
     constant tau (the engine's force t e^(-t / tau)) and the noise's level, centre and decay fitted through the
     engine's own first 150 ms, emulated in closed form (fit_attack_noise): the timbre score's spectro-temporal grid and
     attack measures are the loss (automated refinement against the recording)
  6. (round 190nl) mode families across each layer's keys (keyfit.families, the LPM mode's family byte); the strike is
     then refined through the engine itself by tools/physmod/refine.py, which writes the shipped tables and includes
     (round 190nm: refine.py --level then matches each anchor's level through the engine)

Round 190nm settings (per instrument in INSTRUMENTS, or --set for a variant):
  window_exact   a mode's level corrected for its tracking window (a fast partial read through a long window - chosen to
                 resolve a weak neighbour - read up to 37 dB too loud), and a partial dying within its window tracked
                 again with a window of half its T60 (modal_analysis.analyse_modes)
  split          the SPLIT_TOP strongest modes each fitted as two components where two follow the track clearly better
                 (modal_analysis.fit_two): a two-stage decay (a tine's fast start and slow tail through its coupling to
                 the body) or a beating pair (a tube's near-degenerate doublet); the second component may start in
                 antiphase (LPM shape byte bit 7)
  cover, peaks   the `long` extra modes chosen by what they add to their third-octave band (select_modes); peaks: how
                 many spectral peaks are analysed (56)
  onset          the strike's slow start as the whole note's onset 1 - e^(-t / rise) (LPM header byte 14) instead of the
                 resonated modes' build-up twins; onset_law=2 (round 190nn): the table asks for the S-shaped onset
                 (1 - e^(-t / rise))^2 (the fit's first guess still emulates the exponential one; refine.py retunes the
                 rise through the engine)
  fade='power'   between two recorded keys a mode only one of them has fades by its power (LPM header byte 15)
  fmin_hz        (round 190nn) the analysis floor in Hz where fmin x the key lies above it (the lines under a high key)
  sympathetic    'flag': lines found in 3+ other takes are kept absolute (in Hz, LPM family 255), and with fmin_hz so is
                 every line below fmin x the key; 'below' (round 190nn): only the lines below fmin x the key (the body,
                 the frame, the other tubes ringing along) are absolute
"""
import datetime
import hashlib
import json
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import keyfit  # noqa: E402
import lpm  # noqa: E402
import modal_analysis as ma  # noqa: E402

TOOL_VERSION = '190nn-1'
NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}

# the instruments: VCSL folder (as fetch_vcsl.py names it), velocity layers in order, modes kept, mode series (ratios to
# the main mode; None = the uniform free-free law (2k+1)^2), beam (0 free-free, 1 clamped-free), position mapping
# (round 190nl) long=N: after the `modes` strongest by energy, up to N more by audible duration (the quiet partials that
# ring longest: the score's spectro-temporal part sees every slice they are heard in); fmin re the key's frequency
INSTRUMENTS = {
    'marimba': dict(set='marimba', layers=['soft', 'med', 'loud'], modes=24, series=[1.0, 3.95, 9.5, 16.5, 25.0], beam=0,
                    pos=(0.25, 0.5, 0.95), fmin=0.85, seconds=8.0),
    'vibraphone': dict(set='vibraphone', layers=['v1', 'v2'], modes=24, series=[1.0, 3.98, 9.8, 17.0], beam=0,
                       pos=(0.25, 0.5, 0.95), fmin=0.85, seconds=12.0),
    'xylophone': dict(set='xylophone', layers=['pp', 'ff'], modes=24, series=[1.0, 3.0, 6.0, 9.9, 14.5], beam=0,
                      pos=(0.25, 0.5, 0.95), fmin=0.85, seconds=4.0),
    'glockenspiel': dict(set='glockenspiel', layers=['soft', 'medium', 'loud'], modes=20, series=None, beam=0,
                         pos=(0.25, 0.5, 0.95), fmin=0.85, seconds=8.0),
    # (round 190nm: fmin 0.28 - the tubes' hum near 0.31 x the strike note rings 3-8 s and was cut at 0.4 -, two-component
    # modes - the doublets of a tube - and the whole note's onset: a bell's strike rises in about 1.8 ms)
    'tubularbells': dict(set='tubularbells', layers=['v2', 'v4'], modes=32, series=None, beam=0, pos=(0.0, 0.02, 0.35),
                         fmin=0.28, seconds=14.0, split=True, onset=True),
    # (the kalimba's other tines ring along in every take: sympathetic='flag' (round 190nl) keeps their lines and marks
    # them absolute - LPM family 255, played at their own pitch whatever key is struck, so a key between two recorded
    # ones hears the same tines ring; sympathetic=True would drop them)
    # (fmin 0.3: the lower tines ring along too, below the plucked one; round 190nl)
    # (round 190nm: the window-exact levels, two-component modes, band-covering long modes from 80 peaks, the whole
    # note's onset - a pluck starts in 2-10 ms - and the equal-power fade of the lines only one neighbour has)
    # (round 190nn: fmin_hz 60 - under a high tine the analysis reaches the lower tines and the body ringing along, kept
    # in Hz -, and the S-shaped onset (LPM onset 2): a pluck's quiet first moment, then the rise)
    'kalimba': dict(set='kalimba', layers=[None], modes=28, series=[1.0, 6.27, 17.55, 34.39], beam=1,
                    pos=(0.2, 0.95, 0.55), fmin=0.3, seconds=6.0, sympathetic='flag', window_exact=True, split=True,
                    cover=True, long=16, peaks=80, onset=True, fade='power', fmin_hz=60.0, onset_law=2),
}


PIPELINE = ('calibrate.py fit (modes, tuning, strike first guess), then refine.py (the strike refined through the engine '
            'against the timbre score\'s attack and spectro-temporal parts; round 190nm: then each anchor\'s level matched '
            'through the engine, --level); rounds 190nl, 190nm, 190nn')
LONG_MODES = 12     # (the default `long`)
NOISE_ORDER = 2     # the engine's strike noise: two band-pass sections (LPM header byte 13)
MAX_LANES = 44      # modes per anchor at most: the engine's 48 lanes less the build-up twins


def audible_seconds(m, top_a0):
    """how long a mode stays within 50 dB of the note's strongest one: its T60 x (its level + 50 dB) / 60 dB"""
    lvl = 20 * np.log10(max(m['a0'], 1e-12) / max(top_a0, 1e-12))
    return max(lvl + 50.0, 0.0) / 60.0 * min(m['t60'], 60.0)


def select_modes(modes, main, n_energy, n_long, cover=False):
    """the `n_energy` strongest modes by energy (a0^2 min(T60, 3 s)), then up to `n_long` more by audible duration
    (round 190nl), the main mode always. cover (round 190nm): the `n_long` by what they add to their third-octave band -
    their audible duration beyond the longest kept mode's in that band (the timbre score's spectro-temporal cells are
    third-octave bands: a quiet high partial alone in its band is heard there for its whole duration, a quiet partial
    next to a louder, longer one adds nothing)"""
    top = max(m['a0'] for m in modes)
    by_e = sorted(modes, key=lambda m: -m['a0'] ** 2 * min(m['t60'], 3.0))
    kept = by_e[:n_energy]
    if cover:
        band = lambda m: int(np.floor(3.0 * np.log2(m['f'] / 1000.0) + 0.5))
        have = {}
        for m in kept:
            have[band(m)] = max(have.get(band(m), 0.0), audible_seconds(m, top))
        rest = list(by_e[n_energy:])
        for _ in range(n_long):
            gains = [(audible_seconds(m, top) - have.get(band(m), 0.0), i) for i, m in enumerate(rest)]
            if not gains:
                break
            g, i = max(gains)
            if g <= 0.05:
                break
            m = rest.pop(i)
            kept.append(m)
            have[band(m)] = max(have.get(band(m), 0.0), audible_seconds(m, top))
    else:
        rest = sorted(by_e[n_energy:], key=lambda m: -audible_seconds(m, top))
        kept += [m for m in rest[:n_long] if audible_seconds(m, top) > 0.05]
    kept = kept[:MAX_LANES]
    if main not in kept:
        kept = [main] + kept[:-1]
    return kept


def write_inc(instrument, data, inc_dir):
    """src/core/physmod/tables/<instrument>.inc: the table's bytes as a C array"""
    os.makedirs(inc_dir, exist_ok=True)
    name = 'lpm_' + instrument
    lines = [', '.join(str(b) for b in data[i:i + 40]) + ',' for i in range(0, len(data), 40)]
    with open(os.path.join(inc_dir, instrument + '.inc'), 'w', newline='\n') as f:
        f.write('// Generated by tools/physmod/calibrate.py from resources/physmod/%s.lpm (%d bytes, SHA-256 %s). Do not edit.\n'
                % (instrument, len(data), hashlib.sha256(data).hexdigest()))
        f.write('alignas (16) static const unsigned char %s[%d] = {\n' % (name, len(data)))
        f.write('\n'.join(lines))
        f.write('\n};\n')


FREE_BETA = [4.730040745, 7.853204624, 10.99560784, 14.13716549, 17.27875966, 20.42035225, 23.56194490, 26.70353756]
CLAMPED_BETA = [1.875104069, 4.694091133, 7.854757438, 10.99554073, 14.13716839, 17.27875953, 20.42035225, 23.56194490]


def mode_shape(beam, shape, u):
    """the engine's modeShape (LynxModel.cpp): bending mode `shape` of a free-free (0) or clamped-free (1) beam at u"""
    if shape <= 0:
        return 1.0
    k = shape - 1
    if k < 8:
        b = (CLAMPED_BETA if beam == 1 else FREE_BETA)[k]
        bx = b * u
        if beam == 1:
            s = (np.cosh(b) + np.cos(b)) / (np.sinh(b) + np.sin(b))
            return float(np.cosh(bx) - np.cos(bx) - s * (np.sinh(bx) - np.sin(bx)))
        s = (np.cosh(b) - np.cos(b)) / (np.sinh(b) - np.sin(b))
        return float(np.cosh(bx) + np.cos(bx) - s * (np.sinh(bx) + np.sin(bx)))
    b = (2 * shape - 1) * np.pi / 2 if beam == 1 else (2 * shape + 1) * np.pi / 2
    return float(np.sqrt(2) * (np.sin(b * u - np.pi / 4) if beam == 1 else np.cos(b * u + np.pi / 4)))


def label_key(name):
    m = re.search(r'_([A-G])(#?)(\d)_', name)
    return 12 * (int(m.group(3)) + 1) + NOTE[m.group(1)] + (1 if m.group(2) else 0) + 12


def layer_of(name, layers):
    if layers == [None]:
        return 0
    for i, l in enumerate(layers):
        if '_%s_' % l in name:
            return i
    return None


def hz(key):
    return 440.0 * 2 ** ((key - 69) / 12.0)


def cents(a, b):
    return 1200.0 * np.log2(a / b)


def shape_of(ratio, series, beam):
    """the bending-mode number of a mode at `ratio` (to the main mode), 0 when it fits no mode of the series"""
    if series is None:  # uniform free-free beam: f_k ~ (2k+1)^2 with the main mode as k = kmain
        return None
    best, bd = 0, 1e9
    for k, s in enumerate(series):
        d = abs(cents(ratio, s))
        if d < bd:
            best, bd = k + 1, d
    return best if bd < 200 else 0


def uniform_shapes(modes, main_f):
    """free-free bars and tubes without a tuned series: the main mode's order from the strong modes' frequency pattern
    ((2k+1)^2 law), then each mode's order; 0 where a mode sits between orders"""
    strong = sorted([m['f'] for m in modes[:6]])
    best = None
    for kmain in range(1, 9):
        c = main_f / (2 * kmain + 1) ** 2
        err = 0.0
        for f in strong:
            k = (np.sqrt(f / c) - 1) / 2
            err += min(abs(k - np.round(k)), 0.5)
        if best is None or err < best[0] - 1e-9:
            best = (err, kmain, c)
    _, kmain, c = best
    out = []
    for m in modes:
        k = (np.sqrt(m['f'] / c) - 1) / 2
        out.append(int(np.round(k)) if abs(k - np.round(k)) < 0.2 and np.round(k) >= 1 else 0)
    return out, kmain


def noise_fit(x, sr, modes_all, main_a0):
    """strike noise: the first 100 ms with every detected mode notched out (+-max(1.5 %, 4 bins))"""
    n = int(0.1 * sr)
    seg = x[:n].copy()
    N = 1 << int(np.ceil(np.log2(n * 2)))
    X = np.fft.rfft(seg, N)
    fr = np.fft.rfftfreq(N, 1.0 / sr)
    for m in modes_all:
        X[np.abs(fr - m['f']) < max(0.015 * m['f'], 4 * sr / N)] = 0
    X[fr < 60] = 0
    r = np.fft.irfft(X, N)[:n]
    hop = int(0.002 * sr)
    k = len(r) // hop
    e = np.sqrt(np.mean(r[:k * hop].reshape(k, -1) ** 2, 1)) + 1e-12
    edb = 20 * np.log10(e)
    pk = int(np.argmax(edb[:10]))
    t = (np.arange(k) + 0.5) * hop / sr
    floor = np.percentile(edb, 20)
    use = np.arange(k) >= pk
    use &= edb > max(floor + 3, edb[pk] - 40)
    if use.sum() >= 3:
        sl = np.polyfit(t[use], edb[use], 1)[0]
        tau_ms = float(np.clip(8.686 / max(-sl, 1e-3) * 1000.0, 2.0, 120.0))
    else:
        tau_ms = 10.0
    a20 = int(0.02 * sr)
    S = np.abs(np.fft.rfft(r[:a20] * np.hanning(a20))) ** 2
    f2 = np.fft.rfftfreq(a20, 1.0 / sr)
    S[f2 < 60] = 0
    tot = S.sum() + 1e-30
    fc = float((S * f2).sum() / tot)
    spread = float(np.sqrt((S * (f2 - fc) ** 2).sum() / tot))
    q = float(np.clip(fc / max(2 * spread, 1.0), 0.5, 4.0))
    fc = float(np.clip(fc, 150.0, 0.4 * sr))
    # level: the residual's peak 2 ms RMS re the main mode's amplitude, as the engine's normalised band-pass noise:
    # white uniform noise (RMS 1/sqrt(3)) through a unity-peak band-pass passes ENBW = (pi/2) fc / Q of fs / 2
    chain = (1 / np.sqrt(3)) * np.sqrt(np.pi * fc / (q * sr))
    level = float(np.clip(20 * np.log10(e[pk] / (main_a0 * chain + 1e-12)), -60.0, 20.0))
    return {'noiseDb': level, 'noiseHz': fc, 'noiseQ': q, 'noiseMs': tau_ms}


# --- the engine's first 150 ms, emulated (src/core/physmod/LynxModel.cpp at its default knobs) ------------------------
EMU_SECONDS = 0.15
EMU_EDGES = np.array([0.0, 0.003, 0.006, 0.012, 0.025, 0.05, 0.1, 0.15])
TAUS = np.geomspace(0.00003, 0.002, 11)
RISES = np.array([0.0, 0.002, 0.004, 0.008, 0.016])
H_DRIVE = 0.1  # LynxModel.cpp hDrive
NOISE_T = np.array([0.0015, 0.003, 0.006, 0.012, 0.025, 0.05])
NOISE_DB = np.arange(-50.0, 21.0, 3.0)


def with_build_up(modes, main_f, rise):
    """the resonated modes (within 6 % of the main mode) with their build-up: a twin of opposite sign decaying with the
    time constant `rise` (the engine's twin lanes), so the pair grows as e^(-t/T) - e^(-t/rise)"""
    if rise <= 0:
        return modes
    out = list(modes)
    for f, a0, t60, sign in modes:
        if abs(f / main_f - 1) < 0.06:
            out.append((f, a0, 6.9078 * rise, -sign))
    return out


def emulate_modes(modes, sr, n, tau):
    """the modal part: each mode (f, a0, t60, sign) driven by the contact pulse t e^(-t/tau) as the engine does it
    (two one-pole low-passes on an impulse, the mode's drive a0 / |H(w)| so its amplitude after the strike is a0; a
    mode the pulse reaches less than H_DRIVE gets a0 as a kick at the force's peak):
    closed form y[n] = Im(d (1-c)^2 g[n]), g = 1 / ((1 - p z^-1)(1 - c z^-1)^2) by partial fractions"""
    c = np.exp(-1.0 / max(tau * sr, 0.05))
    t = np.arange(n)
    y = np.zeros(n)
    for f, a0, t60, sign in modes:
        w = 2 * np.pi * f / sr
        if w >= 2 * np.pi * 0.45:
            continue
        r = 10 ** (-3.0 / (t60 * sr))
        p = r * np.exp(1j * w)
        h = (1 - c) ** 2 / (1 - 2 * c * np.cos(w) + c * c)
        if h < H_DRIVE:  # (the engine kicks it at the force's peak)
            k = int(np.argmax((1 - c) ** 2 * (t + 1) * c ** t))
            y[k:] += np.imag(sign * a0 * p ** (t[: n - k] + 1))
            continue
        d = sign * a0 / h
        al = p * p / (p - c) ** 2
        ga = c / (c - p)
        be = 1 - al - ga
        g = al * p ** t + (be + ga * (t + 1)) * c ** t
        y += np.imag(d * (1 - c) ** 2 * g)
    return y


def noise_env(sr, n, tau, T):
    """the strike noise's envelope: the contact force normalised to its peak while it rises, then e^(-t / T)"""
    c = np.exp(-1.0 / max(tau * sr, 0.05))
    t = np.arange(n)
    force = (1 - c) ** 2 * (t + 1) * c ** t
    pk = int(np.argmax(force))
    env = force / force[pk]
    env[pk:] = np.exp(-(t[pk:] - pk) / (T * sr))
    return env


def noise_carrier(sr, n, fc, q, seed=7, order=None):
    """white noise of RMS 1/sqrt(3) (the engine's uniform noise) through a unity-peak band-pass at fc, Q (the TPT
    state-variable filter's analog prototype, pre-warped), `order` sections (NOISE_ORDER)"""
    rng = np.random.default_rng(seed)
    N = 1 << int(np.ceil(np.log2(n * 2)))
    X = np.fft.rfft(rng.standard_normal(N) / np.sqrt(3.0))
    f = np.fft.rfftfreq(N, 1.0 / sr)
    W = np.tan(np.pi * np.minimum(f, 0.499 * sr) / sr) / np.tan(np.pi * fc / sr)
    H = (W / q) / np.sqrt((1 - W * W) ** 2 + (W / q) ** 2 + 1e-30)
    return np.fft.irfft(X * H ** (order or NOISE_ORDER), N)[:n]


ONSETS = np.array([0.0, 0.00025, 0.0005, 0.001, 0.002, 0.004])


def onset_env(sr, n, rise):
    """the engine's whole-note onset (LPM onset tables, round 190nm): 1 - e^(-t / rise)"""
    if rise <= 0:
        return np.ones(n)
    return 1.0 - np.exp(-np.arange(n) / (rise * sr))


def fit_attack_noise(x, sr, modes, main_a0, fc0, q0, floor=None, main_f=None, onset=False):
    """contact time constant and strike noise (level re the main mode, centre, Q, decay) that make the emulated first
    150 ms closest to the recording: the timbre score's spectro-temporal grid (third-octaves x 0-3-6-12-25-50-100-150 ms,
    absolute levels) and its attack (10-90 % rise, the 1 ms envelope on a log time grid), grid search"""
    n = int(EMU_SECONDS * sr)
    ref = x[:n]
    gr = ma.band_energy_grid(ref, sr, EMU_EDGES)
    top = gr.max()
    fl = (floor if floor is not None else np.full((gr.shape[0], 1), -300.0)) + 6.0  # (the timbre score's background rule)
    e_ref = ma.smooth_env(ref, sr, 0.5)
    rise_ref = ma.rise_of_env(e_ref[::4], sr / 4.0)
    grid_t = np.unique(np.clip((np.geomspace(0.0005, EMU_SECONDS, 32) * sr).astype(int), 0, n - 1))
    curve_ref = 20 * np.log10(ma.smooth_env(ref, sr, 1.0)[grid_t] / max(ma.smooth_env(ref, sr, 1.0).max(), 1e-12) + 1e-9)
    fcs = [float(np.clip(fc0 * k, 150.0, 0.4 * sr)) for k in (0.5, 1.0, 2.0)]
    carriers = {fc: noise_carrier(sr, n, fc, q0) for fc in fcs}
    chain = main_a0
    best = None
    import timbre_score as ts

    def shifted(y, lag):
        y = y[lag:] if lag > 0 else np.concatenate([np.zeros(-lag), y]) if lag < 0 else y
        return np.concatenate([y, np.zeros(max(0, n - len(y)))])[:n]

    m = curve_ref > -40
    noise_cache = {}
    for tau, rise in [(t, r) for t in TAUS for r in (ONSETS if onset else RISES if main_f else [0.0])]:
        env = onset_env(sr, n, rise) if onset else None
        ym = emulate_modes(modes if onset else with_build_up(modes, main_f, rise), sr, n, tau)
        if onset:
            ym = ym * env
        lag = ts.envelope_lag(ref, ym, sr)  # (the timbre score lines the notes up so; the noise moves with the modes)
        ym = shifted(ym, lag)
        Em = 10 ** (ma.band_energy_grid(ym, sr, EMU_EDGES) / 10)
        em2 = ma.smooth_env(ym, sr, 0.5) ** 2
        em2c = ma.smooth_env(ym, sr, 1.0) ** 2
        for fc in fcs:
            for T in NOISE_T:
                key = (tau, fc, T, lag, rise if onset else 0.0)
                if key not in noise_cache:
                    yn = shifted(carriers[fc] * noise_env(sr, n, tau, T) * chain * (env if onset else 1.0), lag)
                    noise_cache[key] = (10 ** (ma.band_energy_grid(yn, sr, EMU_EDGES) / 10), ma.smooth_env(yn, sr, 0.5) ** 2,
                                        ma.smooth_env(yn, sr, 1.0) ** 2)
                En, en2, en2c = noise_cache[key]
                for ndb in list(NOISE_DB) + [None]:
                    L2 = 0.0 if ndb is None else 10 ** (ndb / 10)
                    ge = 10 * np.log10(Em + L2 * En + 1e-30)
                    aud = ((gr > top - 50) & (gr > fl)) | ((ge > top - 50) & (ge > fl))
                    spec = float(np.mean(np.minimum(np.abs(ge[aud] - gr[aud]), 30.0))) / 10.0
                    env = np.sqrt(em2 + L2 * en2)
                    # (the rise on the envelope decimated by 4: 0.09 ms steps, well under the score's 0.5 ms floor)
                    rise_m = abs(np.log2(max(ma.rise_of_env(env[::4], sr / 4.0), 1e-5) / max(rise_ref, 1e-5))) / 1.5
                    ec = np.sqrt(em2c + L2 * en2c)
                    curve_e = 20 * np.log10(ec[grid_t] / max(ec.max(), 1e-12) + 1e-9)
                    curve = float(np.mean(np.minimum(np.abs(curve_e[m] - curve_ref[m]), 30.0))) / 8.0 if m.any() else 1.0
                    att = (min(rise_m, 1.0) + min(curve, 1.0)) / 2.0
                    loss = 1.5 * min(spec, 1.0) ** 2 + att ** 2
                    if best is None or loss < best[0]:
                        best = (loss, tau, fc, T, ndb, spec, att, rise)
    loss, tau, fc, T, ndb, spec, att, rise = best
    return {'contact': float(tau), 'riseMs': float(rise * 1000.0), 'noiseDb': float(ndb) if ndb is not None else -60.0, 'noiseHz': fc, 'noiseQ': float(q0),
            'noiseMs': float(T * 1000.0), 'fit_loss': round(float(loss), 4), 'fit_spec_miss': round(spec, 3), 'fit_attack_miss': round(att, 3),
            'rise_ref_ms': round(rise_ref * 1000.0, 3)}


SPLIT_TOP = 16       # (round 190nm) the strongest modes by energy tried as two components


def split_modes(x, sr, kept, main, cfg):
    """round 190nm: the strongest SPLIT_TOP kept modes, each fitted as two components on its complex track
    (modal_analysis.fit_two: a two-stage decay or a beating pair); a mode whose track two components follow clearly
    better becomes two modes (part 1 the stronger at the strike, part 2 the other, its sign as fitted). The lanes stay
    within MAX_LANES: each split takes the place of the weakest unsplit mode (never the main mode)."""
    hop = max(1, int(sr * 0.005))
    order = sorted(kept, key=lambda m: -m['a0'] ** 2 * min(m['t60'], 3.0))
    out = list(kept)
    tried = 0
    for m in order:
        if tried >= cfg.get('split_top', SPLIT_TOP):
            break
        if m.get('absolute') or not any(q is m for q in out):
            continue  # (an absolute line, or a weak mode an earlier split took the place of)
        tried += 1
        t, z = ma.track_complex(x, sr, m['f'], m['win'], hop)
        single = ma.fit_decay(t, np.abs(z))
        two = ma.fit_two(t, z, single, max_split_hz=cfg.get('split_hz', 3.0), min_gain_db=cfg.get('split_gain_db', 0.6))
        if two is None:
            continue
        if len(out) + 1 > MAX_LANES:
            spare = [q for q in sorted(out, key=lambda q: q['a0'] ** 2 * min(q['t60'], 3.0)) if q is not main and not q.get('part')
                     and q is not m]
            if not spare:
                break
            out.remove(spare[0])
        if cfg.get('window_exact'):
            for p in two['parts']:
                p['a0'] /= ma.window_gain(p['t60'], m['win'], sr)
        p1, p2 = sorted(two['parts'], key=lambda p: -p['a0'])
        # (the main mode's stronger part stays at the main mode's frequency: the note's tuning and the key's ratio 1)
        shift = p1['df'] if m is main else 0.0
        a = dict(m, f=m['f'] + p1['df'] - shift, a0=p1['a0'], t60=p1['t60'], part=1, split=two)
        b = dict(m, f=m['f'] + p2['df'] - shift, a0=p2['a0'], t60=p2['t60'], part=2, invert=p2['sign'] * p1['sign'] < 0, sibling=a)
        for q in (a, b):
            q['energy'] = q['a0'] ** 2 * q['t60']
        i = next(j for j, q in enumerate(out) if q is m)
        out[i:i + 1] = [a, b]
    return out


def fit_note(path, cfg, sympathetic_lines=None):
    x, sr = ma.read_wav(path)
    on = ma.onset(x, sr)
    x = x[on:][:int(cfg['seconds'] * sr)]
    name = os.path.basename(path)
    key = label_key(name)
    f_nom = hz(key)
    # (round 190nn: fmin_hz - the analysis reaches down to this many Hz where fmin x the key lies above it; with
    # sympathetic='flag' a line found there - below fmin x the key, another tine or the body - is kept in Hz, absolute)
    floor_hz = min(cfg['fmin'] * f_nom, cfg.get('fmin_hz', 1e9))
    modes = ma.analyse_modes(x, sr, fmin=floor_hz, k=cfg.get('peaks', 56), prom_db=10.0, window_exact=cfg.get('window_exact', False))
    if not modes:
        raise ValueError('no modes: %s' % name)
    top = max(m['a0'] for m in modes)
    modes = [m for m in modes if m['a0'] >= top * 1e-3 and m['t60'] > 0.01]
    near = [m for m in modes if abs(cents(m['f'], f_nom)) < 70]
    main = max(near, key=lambda m: m['energy']) if near else max(modes, key=lambda m: m['energy'])
    offset = cents(main['f'], f_nom) if near else 0.0
    strongest = max(modes, key=lambda m: m['energy'])
    octave_check = round(float(np.log2(strongest['f'] / f_nom)), 2)
    f_tuned = f_nom * 2 ** (offset / 1200)
    dropped = []
    if cfg.get('sympathetic') == 'below':
        # (round 190nn: only the lines below fmin x the key - the body, the frame, the other tubes - kept in Hz)
        for m in modes:
            if m['f'] < cfg['fmin'] * f_nom and m is not main:
                m['absolute'] = True
    elif sympathetic_lines is not None:
        keep = []
        for m in modes:
            r = m['f'] / main['f']
            own = any(abs(cents(r, s)) < 60 for s in cfg['series']) or m is main
            hits = sum(1 for other in sympathetic_lines if any(abs(cents(m['f'], g)) < 8 for g in other))
            if m['f'] < cfg['fmin'] * f_nom and m is not main and cfg.get('sympathetic') == 'flag':
                m['absolute'] = True
                keep.append(m)
            elif hits >= 3 and not own:
                if cfg.get('sympathetic') == 'flag':
                    m['absolute'] = True
                    keep.append(m)
                else:
                    dropped.append(round(m['f'], 1))
            else:
                keep.append(m)
        modes = keep
    kept = select_modes(modes, main, cfg['modes'], cfg.get('long', LONG_MODES), cfg.get('cover', False))
    if cfg.get('split'):
        kept = split_modes(x, sr, kept, main, cfg)
    # (round 190nm: the two parts of a split mode share their mode's shape - one physical mode)
    whole = [m for m in kept if m.get('part', 0) != 2]
    if cfg['series'] is None:
        shapes, kmain = uniform_shapes(sorted(whole, key=lambda m: -m['energy']), main['f'])
        order = sorted(whole, key=lambda m: -m['energy'])
        shape = {id(m): s for m, s in zip(order, shapes)}
    else:
        shape = {id(m): shape_of(m['f'] / main['f'], cfg['series'], cfg['beam']) for m in whole}
        kmain = 1
    # one mode per bending order (the strongest); the rest keep no position law
    seen = {}
    for m in sorted(whole, key=lambda m: -m['energy']):
        s = shape[id(m)]
        if s and s in seen:
            shape[id(m)] = 0
        elif s:
            seen[s] = m
    for m in kept:
        if m.get('part', 0) == 2:
            shape[id(m)] = shape[id(m['sibling'])]
    # strike: a first guess of the noise's band from the notched residual, then the contact and the noise fitted through
    # the engine's emulated first 150 ms (the modes kept, with their strike-point signs at the default position)
    guess = noise_fit(x, sr, modes, main['a0'])
    u_ref = cfg['pos'][1] + (cfg['pos'][2] - cfg['pos'][1]) * cfg['pos'][0]
    emu = [(m['f'], m['a0'], min(m['t60'], 60.0), (-1.0 if m.get('invert') else 1.0) *
            (-1.0 if shape[id(m)] and mode_shape(cfg['beam'], shape[id(m)], u_ref) < 0 else 1.0)) for m in kept]
    import timbre_score as ts
    floor = ts.band_floor(ma.band_energy_grid(x, sr, ts.time_edges(len(x) / sr)))
    nz = fit_attack_noise(x, sr, emu, main['a0'], guess['noiseHz'], guess['noiseQ'], floor, main['f'], cfg.get('onset', False))
    contact = nz.pop('contact')
    rise = nz['rise_ref_ms'] / 1000.0
    # (ratio = f / (the key's equal-tempered frequency x the tuning offset): the main mode lands on ratio 1, in tune)
    # (an absolute line - another tine - refers to the key's equal-tempered frequency, not the take's tuning: played at
    # the take's tuning it must land on its own frequency, the engine keeps it in Hz)
    out_modes = [{'ratio': float(m['f'] / (f_nom if m.get('absolute') else f_tuned)),
                  'ampDb': float(20 * np.log10(m['a0'] / main['a0'])), 't60': float(min(m['t60'], 60.0)),
                  'shape': 0 if m.get('absolute') else int(shape[id(m)]), 'absolute': bool(m.get('absolute')),
                  'f_hz': m['f'], 'fit_rms_db': m['rms_db'], **({'part': m['part'], 'invert': bool(m.get('invert'))} if m.get('part') else {})}
                 for m in sorted(kept, key=lambda m: m['f'])]
    return {'file': name, 'key': key, 'tuning_offset_cents': round(float(offset), 2), 'label_octave_check': octave_check,
            'main_hz': main['f'], 'gainDb': float(20 * np.log10(main['a0'])), 'contact': contact, 'rise_ms': rise * 1000.0,
            'kmain': kmain, 'modes': out_modes, 'dropped_sympathetic_hz': dropped, 'sr': sr, **nz}


def notes_of(root, cfg, manifest):
    files = [f for f in manifest['files'] if f['set'] == cfg['set']]
    out = []
    for f in files:
        name = f['path'].split('/')[-1]
        layer = layer_of(name, cfg['layers'])
        if layer is None:
            continue
        out.append((label_key(name), layer, name, f))
    out.sort(key=lambda t: (t[0], t[1], t[2]))
    # one take per (key, layer): the first by name; the others are kept aside as extra checks (another tine, a 2nd take)
    first, extra = [], []
    seen = set()
    for t in out:
        (extra if (t[0], t[1]) in seen else first).append(t)
        seen.add((t[0], t[1]))
    return first, extra


def calibrate(instrument, root, manifest, outdir, holdout=0, inc_dir=None, report_dir=None, fold=0):
    cfg = INSTRUMENTS[instrument]
    takes, extra = notes_of(root, cfg, manifest)
    keys = sorted(set(t[0] for t in takes))
    held = set(keys[1 + fold::holdout]) if holdout else set()
    held.discard(keys[0]); held.discard(keys[-1])  # (the range's ends stay: held-out notes test interpolation only)
    use = [t for t in takes if t[0] not in held]
    sym = None
    if cfg.get('sympathetic') and cfg.get('sympathetic') != 'below':
        sym = []
        for t in takes:
            x, sr = ma.read_wav(os.path.join(root, cfg['set'], t[2]))
            x = x[ma.onset(x, sr):][:int(cfg['seconds'] * sr)]
            sym.append([m['f'] for m in ma.analyse_modes(x, sr, fmin=60.0, k=40, prom_db=10.0)])
    fits = []
    for key, layer, name, f in use:
        path = os.path.join(root, cfg['set'], name)
        others = None
        if sym is not None:
            idx = [i for i, t in enumerate(takes) if t[2] == name][0]
            others = [s for i, s in enumerate(sym) if i != idx and takes[i][0] != key]
        r = fit_note(path, cfg, others)
        nl = len(cfg['layers'])
        r['velocity'] = 0.8 if nl == 1 else (layer + 1) / nl
        r['layer'] = cfg['layers'][layer]
        r['source'] = f
        fits.append(r)
        print('  %-14s %-46s key %3d off %+6.1f c  modes %2d  contact %.3f ms  noise %5.1f dB %6.0f Hz' % (
            instrument, name[:46], key, r['tuning_offset_cents'], len(r['modes']), r['contact'] * 1000, r['noiseDb'], r['noiseHz']), flush=True)
    # the instrument's level: the loudest layer's notes peak around -8 dBFS (the sum of their mode amplitudes)
    top = max(f['velocity'] for f in fits)
    peaks = [f['gainDb'] + 20 * np.log10(sum(10 ** (m['ampDb'] / 20) for m in f['modes'])) for f in fits if f['velocity'] == top]
    gain_offset = float(-8.0 - np.median(peaks))
    pos_ref, u0, u1 = cfg['pos']
    anchors = [{'key': float(f['key']), 'velocity': float(f['velocity']), 'gainDb': float(f['gainDb']), 'contact': f['contact'], 'riseMs': f['riseMs'],
                'noiseDb': f['noiseDb'], 'noiseHz': f['noiseHz'], 'noiseQ': f['noiseQ'], 'noiseMs': f['noiseMs'],
                'modes': [dict({k: m[k] for k in ('ratio', 'ampDb', 't60', 'shape')}, **({'fam': lpm.ABSOLUTE} if m.get('absolute') else {}),
                               **({'part': m['part'], 'invert': m['invert']} if m.get('part') else {}))
                          for m in f['modes']]} for f in fits]
    keyfit.families(anchors)  # (round 190nl: the engine pairs the same mode across keys by its family)
    tag = instrument + ('-holdout%d' % holdout if holdout else '') + ('f%d' % fold if holdout and fold else '')
    provenance = {
        'instrument': instrument, 'table': tag + '.lpm', 'tool': 'tools/physmod/calibrate.py ' + TOOL_VERSION,
        'date': datetime.date.today().isoformat(),
        'pipeline': PIPELINE,
        'data_policy': 'CC0 only; fitted parameters ship, no audio; synthesised attack (no recorded transient)',
        'source': {'library': 'Versilian Community Sample Library (VCSL)', 'url': manifest['source'], 'commit': manifest['commit'],
                   'licence': manifest['licence'], 'licence_sha256': manifest.get('licence_sha256'),
                   'licence_check': manifest.get('licence_check')},
        'files': [{'path': f['source']['path'], 'git_blob_sha1': f['source']['git_blob_sha1'], 'sha256': f['source']['sha256'],
                   'bytes': f['source']['bytes'], 'licence': 'CC0-1.0', 'layer': f['layer'], 'velocity': f['velocity'],
                   'sounding_key': f['key']} for f in fits],
        'held_out_keys': sorted(held), 'not_used_extra_takes': [t[2] for t in extra],
    }
    pbytes = json.dumps(provenance, indent=1, sort_keys=True).encode()
    os.makedirs(outdir, exist_ok=True)
    data = lpm.write(os.path.join(outdir, tag + '.lpm'), instrument, cfg['beam'], pos_ref, u0, u1, gain_offset, anchors, pbytes,
                     NOISE_ORDER, cfg.get('onset_law', 1) if cfg.get('onset') else 0, 1 if cfg.get('fade') == 'power' else 0)
    open(os.path.join(outdir, tag + '.provenance.json'), 'wb').write(pbytes)
    report = {'instrument': instrument, 'bytes': len(data), 'anchors': len(anchors), 'modes': sum(len(a['modes']) for a in anchors),
              'gain_offset_db': gain_offset, 'held_out_keys': sorted(held),
              'held_out_takes': [{'file': t[2], 'key': t[0], 'layer': cfg['layers'][t[1]]} for t in takes if t[0] in held],
              'extra_takes': [{'file': t[2], 'key': t[0], 'layer': cfg['layers'][t[1]]} for t in extra],
              'notes': fits}
    os.makedirs(report_dir or outdir, exist_ok=True)
    json.dump(report, open(os.path.join(report_dir or outdir, tag + '.fit.json'), 'w'), indent=1)
    if inc_dir and not holdout:
        write_inc(instrument, data, inc_dir)
    print('%s: %d anchors, %d modes, %d bytes, gain offset %.1f dB, held out %s' % (
        tag, len(anchors), report['modes'], len(data), gain_offset, sorted(held)), flush=True)
    return report


def main():
    a = sys.argv[1:]
    if len(a) < 5 or a[0] != 'fit':
        print(__doc__)
        return 2
    inst, root, man, out = a[1], a[2], json.load(open(a[3])), a[4]
    holdout = int(a[a.index('--holdout') + 1]) if '--holdout' in a else 0
    fold = int(a[a.index('--fold') + 1]) if '--fold' in a else 0
    inc = a[a.index('--inc-dir') + 1] if '--inc-dir' in a else None
    rep = a[a.index('--report-dir') + 1] if '--report-dir' in a else None
    # (round 190nm) --set name=value[,name=value]: override the instruments' settings (variants; numbers parse as such)
    if '--set' in a:
        for kv in a[a.index('--set') + 1].split(','):
            k, v = kv.split('=')
            for i in (INSTRUMENTS if inst == 'all' else inst.split(',')):
                INSTRUMENTS[i][k] = json.loads(v)
    for i in (INSTRUMENTS if inst == 'all' else inst.split(',')):
        calibrate(i, root, man, out, holdout, inc, rep, fold)
    return 0


if __name__ == '__main__':
    sys.exit(main())
