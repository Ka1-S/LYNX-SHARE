#!/usr/bin/env python3
"""Unit tests of tools/physmod/keyfit.py and the LPM family byte (round 190nl).

  python3 -m unittest discover -s tools/physmod -p 'test_*.py'
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import keyfit  # noqa: E402
import lpm  # noqa: E402


def anchor(key, modes, vel=1.0, **kw):
    a = {'key': float(key), 'velocity': vel, 'gainDb': -20.0, 'contact': 0.0005, 'riseMs': 0.0, 'noiseDb': -20.0, 'noiseHz': 2000.0,
         'noiseQ': 1.0, 'noiseMs': 10.0, 'modes': [{'ratio': r, 'ampDb': d, 't60': t, 'shape': 0} for r, d, t in modes]}
    a.update(kw)
    return a


class KeyfitTests(unittest.TestCase):
    def test_families_follow_a_drifting_mode(self):
        # a glockenspiel-like second mode drifting 3.04 -> 2.88 -> 2.70 across keys, a weak line near it at one key
        an = [anchor(79, [(1.0, 0, 8), (3.04, -2, 1.2)]), anchor(91, [(1.0, 0, 5), (2.88, 2, 1.0), (3.02, -40, 0.3)]),
              anchor(103, [(1.0, 0, 4), (2.70, 6, 0.3)])]
        keyfit.families(an)
        main = {a['modes'][0]['fam'] for a in an}
        second = [next(m['fam'] for m in a['modes'] if 2.6 < m['ratio'] < 3.1 and m['ampDb'] > -10) for a in an]
        self.assertEqual(len(main), 1)
        self.assertNotIn(0, main)
        self.assertEqual(len(set(second)), 1, second)  # (the strong mode, not the weak line that sits closer)
        self.assertNotEqual(second[0], an[0]['modes'][0]['fam'])

    def test_many_families_never_become_absolute(self):
        # (round 190nm) more than 254 families across a table: no regular mode may end up with the absolute id 255
        an = [anchor(40 + k, [(1.0, 0, 5)] + [(1.5 * 1.2 ** i * (1.1 if k % 2 else 1.0), -20, 1.0) for i in range(30)]) for k in range(12)]
        an[3]['modes'].append({'ratio': 2.222, 'ampDb': -30.0, 't60': 1.0, 'shape': 0, 'fam': lpm.ABSOLUTE})
        keyfit.families(an)
        absolute = [m for a in an for m in a['modes'] if m['fam'] == lpm.ABSOLUTE]
        self.assertEqual(len(absolute), 1)
        self.assertAlmostEqual(absolute[0]['ratio'], 2.222)

    def test_main_modes_pair(self):
        # (round 190nm) the main mode (ratio 1) continues as the main mode even when the next key's main is much louder
        # and a weaker line sits near it (a kalimba tine split in two components)
        an = [anchor(53, [(1.0, 0, 3), (1.0216, -28, 2)]), anchor(55, [(1.0, 15.8, 5), (1.011, -1.5, 2), (1.02, -2.9, 1)])]
        keyfit.families(an)
        self.assertEqual(an[0]['modes'][0]['fam'], an[1]['modes'][0]['fam'])

    def test_layers_are_separate(self):
        an = [anchor(60, [(1.0, 0, 1)], vel=0.5), anchor(72, [(1.0, 0, 1)], vel=0.5), anchor(60, [(1.0, 0, 1)], vel=1.0)]
        keyfit.families(an)
        self.assertEqual(an[0]['modes'][0]['fam'], an[1]['modes'][0]['fam'])
        self.assertEqual(an[2]['modes'][0]['fam'], 0)  # (a family seen once needs no id)

    def test_densify_keeps_recorded_anchors(self):
        an = [anchor(60, [(1.0, 0, 2), (4.0, -10, 0.5)]), anchor(72, [(1.0, 0, 1), (4.2, -14, 0.3)])]
        keyfit.families(an)
        out = keyfit.densify(an, max_gap=4.0)
        self.assertEqual([a['key'] for a in out], [60.0, 64.0, 68.0, 72.0])
        self.assertIs(out[0], an[0])
        mid = out[1]
        self.assertTrue(mid['synthetic'])
        second = [m for m in mid['modes'] if m['ratio'] > 2][0]
        self.assertAlmostEqual(second['ampDb'], -10 - 4 / 3.0, delta=0.01)  # (linear: the default fit)
        self.assertAlmostEqual(second['ratio'], 4.0 * (4.2 / 4.0) ** (1 / 3.0), delta=1e-6)

    def test_lpm_family_round_trip(self):
        an = [anchor(60, [(1.0, 0, 2), (4.0, -10, 0.5)]), anchor(72, [(1.0, 0, 1), (4.2, -14, 0.3)])]
        keyfit.families(an)
        data = lpm.write(None, 'marimba', 0, 0.25, 0.5, 0.95, 0.0, an, b'p', noise_order=2)
        back = lpm.read(data)
        self.assertEqual(back['noiseOrder'], 2)
        self.assertEqual([m['fam'] for m in back['anchors'][0]['modes']], [m['fam'] for m in an[0]['modes']])


if __name__ == '__main__':
    unittest.main()
