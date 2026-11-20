#!/usr/bin/env python3
"""Unit tests of round 190nm's fitting additions: two components per partial (modal_analysis.fit_two: a two-stage
decay, a beating pair, a pair starting in antiphase), the tracking window's correction of a fast partial's level
(modal_analysis.window_gain) and the LPM antiphase bit.

  python3 -m unittest discover -s tools/physmod -p 'test_*.py'
"""
import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lpm  # noqa: E402
import modal_analysis as ma  # noqa: E402

SR = 44100


def two(f1, f2, t1, t2, a1, a2, seconds=6.0, noise=1e-5):
    t = np.arange(int(seconds * SR)) / SR
    rng = np.random.default_rng(3)
    return (a1 * np.exp(-6.9078 * t / t1) * np.sin(2 * np.pi * f1 * t) + a2 * np.exp(-6.9078 * t / t2) * np.sin(2 * np.pi * f2 * t)
            + noise * rng.standard_normal(len(t)))


def fit(x, f=500.0):
    win = ma.track_window(f, [3 * f], SR)
    t, z = ma.track_complex(x, SR, f, win, int(0.005 * SR))
    return ma.fit_two(t, z, ma.fit_decay(t, np.abs(z)))


class TwoPartTests(unittest.TestCase):
    def test_two_stage_decay(self):
        r = fit(two(500, 500, 0.5, 6.0, 1.0, 0.3))
        self.assertIsNotNone(r)
        fast, slow = sorted(r['parts'], key=lambda p: p['t60'])
        self.assertAlmostEqual(fast['t60'], 0.5, delta=0.03)
        self.assertAlmostEqual(slow['t60'], 6.0, delta=0.3)
        self.assertAlmostEqual(slow['a0'] / fast['a0'], 0.3, delta=0.03)
        self.assertTrue(all(abs(p['df']) < 0.05 for p in r['parts']))
        self.assertLess(r['rms_db'], r['rms_db_single'] - 1.0)

    def test_beating_pair(self):
        r = fit(two(500, 500.8, 4.0, 4.0, 1.0, 0.7))
        self.assertIsNotNone(r)
        a, b = sorted(r['parts'], key=lambda p: p['df'])
        self.assertAlmostEqual(b['df'] - a['df'], 0.8, delta=0.02)
        self.assertAlmostEqual(b['a0'] / a['a0'], 0.7, delta=0.05)
        self.assertEqual(a['sign'] * b['sign'], 1.0)

    def test_antiphase_pair(self):
        r = fit(two(500, 500.5, 3.0, 5.0, 1.0, -0.5))
        self.assertIsNotNone(r)
        self.assertEqual(r['parts'][0]['sign'] * r['parts'][1]['sign'], -1.0)

    def test_single_decay_stays_single(self):
        self.assertIsNone(fit(two(500, 500, 3.0, 3.0, 1.0, 0.0)))

    def test_window_gain(self):
        self.assertAlmostEqual(ma.window_gain(100.0, 4096, SR), 1.0, delta=1e-3)
        # a fast partial: the window reads it louder than at its centre, as a direct sum shows
        t60, win = 0.15, 13000
        w = np.hanning(win)
        n = (np.arange(win) - (win - 1) / 2.0) / SR
        g = (w * np.exp(-6.9078 / t60 * n)).sum() / w.sum()
        self.assertAlmostEqual(ma.window_gain(t60, win, SR), g, places=6)
        self.assertGreater(ma.window_gain(t60, win, SR), 10.0)

    def test_fast_partial_level(self):
        # a 0.15 s partial next to a neighbour 20 Hz away (a long tracking window): the corrected level is right
        x = two(1300, 1280, 0.15, 0.3, 1.0, 0.01, seconds=2.0, noise=1e-7)
        modes = ma.analyse_modes(x, SR, fmin=1000, k=4, prom_db=6.0, window_exact=True)
        main = max(modes, key=lambda m: m['a0'])
        self.assertAlmostEqual(20 * np.log10(main['a0']), 0.0, delta=1.5)

    def test_lpm_antiphase_bit(self):
        a = {'key': 60.0, 'velocity': 1.0, 'gainDb': -20.0, 'contact': 0.0005, 'riseMs': 0.0, 'noiseDb': -20.0, 'noiseHz': 2000.0,
             'noiseQ': 1.0, 'noiseMs': 10.0, 'modes': [{'ratio': 1.0, 'ampDb': 0.0, 't60': 1.0, 'shape': 3},
                                                        {'ratio': 1.001, 'ampDb': -6.0, 't60': 2.0, 'shape': 3, 'invert': True}]}
        t = lpm.read(lpm.write(None, 'tubularbells', 0, 0.0, 0.02, 0.35, 0.0, [a], b''))
        m0, m1 = t['anchors'][0]['modes']
        self.assertEqual((m0['shape'], m1['shape']), (3, 3))
        self.assertFalse(m0.get('invert', False))
        self.assertTrue(m1['invert'])


if __name__ == '__main__':
    unittest.main()
