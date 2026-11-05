#!/usr/bin/env python3
"""fxparity.py — sound parity of LYNX's effects against the third-party originals they are inspired by (round tp172b).

The originals are rendered black-box by tools/fxmeasure (a host, public parameter API only) and LYNX by
tools/fxrender, on the same stimuli at matched settings. Each pair of renders is compared exactly like FL's own render
tests (scripts/flref_compare.py: alignment, ESR, level, third-octave spectrum, 50 ms envelope) and scored 0-10 with
the same scale as the FL-native parity tracker (round 170i).

Usage:
  fxparity.py pair <original.wav> <lynx.wav>        metrics + score as JSON
  fxparity.py dir <original-dir> <lynx-dir>         every <name>.wav present in both, one JSON line each
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


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == 'pair':
        print(json.dumps(pair(sys.argv[2], sys.argv[3])))
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
