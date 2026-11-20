#!/usr/bin/env python3
"""Unit tests of tools/physmod/timbre_score.py on synthetic notes with known answers (round 190ni).

  python3 -m unittest discover -s tools/physmod -p 'test_*.py'      (or: python3 tools/physmod/timbre_score.py selftest)

Each test builds its notes from decaying partials (frequency, level, T60, phase, attack time) so the expected
change of each sub-score is known: phase changes must not matter, a detune moves only `pitch`, one moved partial
moves `inharm`, scaled decays move `decay`, a slower attack moves `attack`, a level change moves `level`.
"""
import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modal_analysis as ma  # noqa: E402
import timbre_score as ts  # noqa: E402

SR = 48000
# a marimba-like bar: 1 : 3.93 : 9.2 : 16.3, plus a weak resonator line
BAR = [(523.25, 0.0, 2.0), (523.25 * 3.93, -9.0, 0.45), (523.25 * 9.2, -16.0, 0.18), (523.25 * 16.3, -24.0, 0.09),
       (523.25 * 2.0, -30.0, 1.2)]


def note(partials=BAR, seconds=2.5, rise=0.001, gain_db=-6.0, cents=0.0, decay=1.0, phases=None, sr=SR, noise_db=-90.0,
         seed=1):
    t = np.arange(int(seconds * sr)) / sr
    rng = np.random.default_rng(seed)
    y = np.zeros(len(t))
    for i, (f, db, t60) in enumerate(partials):
        ph = phases[i] if phases is not None else 0.0
        y += 10 ** (db / 20) * np.exp(-6.9078 * t / (t60 * decay)) * np.sin(2 * np.pi * f * 2 ** (cents / 1200) * t + ph)
    a = np.clip(t / rise, 0, 1)
    y *= 0.5 - 0.5 * np.cos(np.pi * a)
    y = np.concatenate([np.zeros(int(0.01 * sr)), y])
    y *= 10 ** (gain_db / 20)  # (a fixed gain: a peak normalisation would change the level with the phases)
    y += 10 ** (noise_db / 20) * rng.standard_normal(len(y))
    return y


def score(a, b, **kw):
    return ts.score_pair(a, b, SR, **kw)


class TimbreScoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ref = note()

    def test_identical_is_ten(self):
        r = score(self.ref, self.ref)
        self.assertEqual(r['timbre_score'], 10.0)
        self.assertTrue(all(v == 10.0 for v in r['subscores'].values()), r['subscores'])

    def test_phase_blind(self):
        rng = np.random.default_rng(7)
        other = note(phases=rng.uniform(0, 2 * np.pi, len(BAR)))
        r = score(self.ref, other)
        # (random phases change the attack's crest and the first 10 ms slices, whose short windows see the abrupt 1 ms
        # onset's splatter differently: physically real, but small; the partial parts must not move)
        self.assertGreaterEqual(r['timbre_score'], 9.3, r['subscores'])
        for k in ('pitch', 'inharm', 'levels', 'decay', 'level'):
            self.assertGreaterEqual(r['subscores'][k], 9.8, k)
        inv = score(self.ref, -self.ref)
        self.assertEqual(inv['timbre_score'], 10.0)
        if inv['wave'] is not None:  # the wave score cannot see this: the error is twice the signal
            self.assertGreater(inv['wave']['esr_db'], 5.0)

    def test_detune_moves_pitch_only(self):
        r = score(self.ref, note(cents=15.0))
        self.assertAlmostEqual(r['pitch_cents'], 15.0, delta=1.0)
        self.assertAlmostEqual(r['subscores']['pitch'], 10 * (1 - 15 / ts.FULL['pitch']), delta=0.5)
        self.assertGreaterEqual(r['subscores']['inharm'], 9.5)
        self.assertGreaterEqual(r['subscores']['decay'], 9.5)

    def test_moved_partial_moves_inharm(self):
        moved = list(BAR)
        f, db, t60 = moved[1]
        moved[1] = (f * 2 ** (20 / 1200), db, t60)
        r = score(self.ref, note(partials=moved))
        self.assertGreaterEqual(r['subscores']['pitch'], 9.8)
        self.assertLess(r['subscores']['inharm'], 9.0)
        row = [q for q in r['partials'] if abs(q['f'] - f) < 5][0]
        self.assertAlmostEqual(row['cents'], 20.0, delta=1.5)

    def test_decay_scale(self):
        r2 = score(self.ref, note(decay=2.0))
        r14 = score(self.ref, note(decay=2 ** 0.5))
        self.assertLess(r2['subscores']['decay'], 1.0)
        self.assertAlmostEqual(r14['subscores']['decay'], 5.0, delta=1.0)
        self.assertAlmostEqual(r14['decay_log2_median'], 0.5, delta=0.08)
        self.assertGreater(r14['timbre_score'], r2['timbre_score'])

    def test_slow_attack(self):
        r = score(self.ref, note(rise=0.012))
        self.assertLess(r['subscores']['attack'], 7.0)
        self.assertGreater(r['attack']['rise_ms'][1], 3 * r['attack']['rise_ms'][0])

    def test_level_only(self):
        r = score(self.ref, note(gain_db=-9.0))
        self.assertAlmostEqual(r['level_db'], -3.0, delta=0.2)
        self.assertAlmostEqual(r['subscores']['level'], 5.0, delta=0.4)
        for k in ('pitch', 'inharm', 'levels', 'decay', 'spectime', 'attack'):
            self.assertGreaterEqual(r['subscores'][k], 9.5, k)

    def test_missing_partials_cost(self):
        r = score(self.ref, note(partials=BAR[:1]))
        full = score(self.ref, note(cents=2.0))
        self.assertLess(r['timbre_score'], full['timbre_score'] - 2.0)
        self.assertEqual(r['partials_matched'].split('/')[0], '1')

    def test_monotonic_in_detune(self):
        s = [score(self.ref, note(cents=c))['timbre_score'] for c in (0.0, 5.0, 12.0, 25.0, 45.0)]
        self.assertEqual(s, sorted(s, reverse=True), s)

    def test_noise_is_not_a_note(self):
        rng = np.random.default_rng(3)
        t = np.arange(len(self.ref)) / SR
        nz = rng.standard_normal(len(self.ref)) * np.exp(-3 * t) * 0.1
        r = score(self.ref, nz)
        self.assertLess(r['timbre_score'], 4.0, r['subscores'])

    def test_other_rate(self):
        a = note(sr=44100)
        b = note(sr=44100, cents=10.0)
        r = ts.score_pair(a, b, 44100)
        self.assertAlmostEqual(r['pitch_cents'], 10.0, delta=1.0)

    def test_stiff_string_b(self):
        f0, B = 110.0, 4e-4
        fs = [n * f0 * np.sqrt(1 + B * n * n) for n in range(1, 13)]
        got = ts.stiff_b(fs)
        self.assertIsNotNone(got)
        self.assertAlmostEqual(got[0], f0, delta=0.05)
        self.assertAlmostEqual(got[1] / B, 1.0, delta=0.02)

    def test_wav_round_trip(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'n.wav')
            ma.write_wav(p, self.ref, SR, bits=24)
            x, sr = ma.read_wav(p)
            self.assertEqual(sr, SR)
            self.assertLess(np.abs(x - self.ref).max(), 2e-7)
            self.assertEqual(score(self.ref, x)['timbre_score'], 10.0)

    def test_clean_note_against_a_noisy_take(self):
        # (round 190nl) a take with a fluctuating room rumble: the clean note itself must score about as well as a
        # copy of the take with the same note, and better than a clean note one semitone off
        rng = np.random.default_rng(11)
        n = len(self.ref)
        t = np.arange(n) / SR
        rumble = np.convolve(rng.standard_normal(n), np.ones(400) / 400, 'same') * (1 + 0.8 * np.sin(2 * np.pi * 0.7 * t)) * 0.02
        take = self.ref + rumble
        clean = score(take, self.ref)
        self.assertGreaterEqual(clean['subscores']['spectime'], 5.0, clean.get('spectime_db'))
        self.assertLess(clean['spectime_db'], clean['spectime_190ni']['db'] - 1.0)  # (the 190ni rule counted the room as missing)
        self.assertGreater(clean['timbre_score'], score(take, note(cents=100.0))['timbre_score'] + 2.0)
        # (the 190ni rule is reported beside, never above the new one for a clean note)
        self.assertLessEqual(clean['timbre_score_190ni'], clean['timbre_score'] + 1e-9)

    def test_strike_loss_matches_the_score_parts(self):
        # (round 190nl) refine.py's loss: zero for the note itself, larger for a slower attack and for a darker strike
        sl = ts.StrikeLoss(self.ref, SR)
        self.assertLess(sl.loss(self.ref), 1e-6)
        slow = sl.loss(note(rise=0.012))
        dark = sl.loss(note(partials=BAR[:2]))
        self.assertGreater(slow, 0.01)
        self.assertGreater(dark, 0.01)


class TuningTests(unittest.TestCase):
    def test_long_ringing_fundamental(self):
        # (round 190nl) evaluate.tuning_of on a vibraphone-like note: a 20 s fundamental 12 cents flat with a -50 dB
        # line 50 cents below it; the tuning is the fundamental's, not the line's
        import evaluate
        f = 440.0 * 2 ** (-12 / 1200.0)
        x = note(partials=[(f, 0.0, 20.0), (440.0 * 2 ** (-50 / 1200.0), -50.0, 20.0), (4 * f, -12.0, 3.0)], seconds=2.0)
        x = x[int(0.01 * SR):]
        self.assertAlmostEqual(evaluate.tuning_of(x, SR, 69), -12.0, delta=1.0)


if __name__ == '__main__':
    unittest.main()
