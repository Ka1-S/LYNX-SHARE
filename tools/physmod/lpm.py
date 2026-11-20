#!/usr/bin/env python3
"""lpm.py - the LYNX Model parameter table file (.lpm, version 1; round 190ni). Read by src/core/physmod/LynxModel.cpp.

Little endian. Fitted parameters only: no audio, no recorded attack (owner decision: synthesised excitation only).

  header (40 bytes)
    char[4] 'LPM1' · u16 version (1) · u16 model (0 marimba, 1 vibraphone, 2 xylophone, 3 glockenspiel, 4 tubular bells,
    5 kalimba) · u16 anchors · u16 modes (all anchors) · u8 beam (0 free-free bar or tube, 1 clamped-free tine) · u8[3] 0
    f32 posRef (the position knob's value the recordings were struck at) · f32 posU0, posU1 (the strike point along the
    bar, 0..1, at position 0 and 1) · f32 gainOffsetDb (instrument level) · u8[8] first bytes of the SHA-256 of the
    provenance record (resources/physmod/<name>.provenance.json)
  anchor (32 bytes each; one recorded note at one velocity layer, sorted by key, then velocity)
    f32 key (sounding MIDI key the ratios refer to) · f32 velocity (0..1, the layer) · f32 gainDb (the main mode's level)
    f32 contact (s, the time constant tau of the contact force t e^(-t / tau) that reproduces the recorded attack)
    f16 noiseDb (strike noise level re the main mode) · f16 noiseHz · f16 noiseQ · f16 noiseMs (decay time constant)
    u16 firstMode · u16 modeCount · f32 riseMs (the resonated modes' build-up time constant, 0 = none: the marimba's
    and vibraphone's tubes re-radiate the fundamental, which grows as e^(-t/T) - e^(-t/rise))
  mode (10 bytes each)
    f32 ratio (frequency / the anchor key's equal-tempered frequency) · f16 ampDb (re the main mode) · f16 t60 (s)
    u8 shape (0: no strike-position law, k >= 1: the k-th bending mode of the beam) · u8 0
"""
import hashlib
import struct

import numpy as np

MAGIC = b'LPM1'
VERSION = 1
MODELS = ['marimba', 'vibraphone', 'xylophone', 'glockenspiel', 'tubularbells', 'kalimba']


def f16(v):
    return np.float16(v).tobytes()


def write(path, model, beam, pos_ref, pos_u0, pos_u1, gain_offset_db, anchors, provenance_bytes):
    """anchors: list of dicts {key, velocity, gainDb, contact, noiseDb, noiseHz, noiseQ, noiseMs, modes: [{ratio, ampDb,
    t60, shape}]}; returns the file's bytes (also written to path)"""
    anchors = sorted(anchors, key=lambda a: (a['key'], a['velocity']))
    total = sum(len(a['modes']) for a in anchors)
    head = MAGIC + struct.pack('<HHHHB3x', VERSION, MODELS.index(model), len(anchors), total, beam)
    head += struct.pack('<4f', pos_ref, pos_u0, pos_u1, gain_offset_db) + hashlib.sha256(provenance_bytes).digest()[:8]
    assert len(head) == 40
    body, modes, first = b'', b'', 0
    for a in anchors:
        body += struct.pack('<4f', a['key'], a['velocity'], a['gainDb'], a['contact'])
        body += f16(a['noiseDb']) + f16(a['noiseHz']) + f16(a['noiseQ']) + f16(a['noiseMs'])
        body += struct.pack('<HHf', first, len(a['modes']), a.get('riseMs', 0.0))
        for m in a['modes']:
            modes += struct.pack('<f', m['ratio']) + f16(m['ampDb']) + f16(m['t60']) + struct.pack('<BB', m['shape'], 0)
        first += len(a['modes'])
    data = head + body + modes
    if path:
        open(path, 'wb').write(data)
    return data


def read(data):
    """the file's content as a dict (the C++ reader's mirror; used by the tests and the reports)"""
    if data[:4] != MAGIC:
        raise ValueError('not an LPM1 file')
    ver, model, na, nm, beam = struct.unpack('<HHHHB', data[4:13])
    pos_ref, u0, u1, gain = struct.unpack('<4f', data[16:32])
    out = {'version': ver, 'model': MODELS[model], 'beam': beam, 'posRef': pos_ref, 'posU0': u0, 'posU1': u1,
           'gainOffsetDb': gain, 'provenance_sha256_8': data[32:40].hex(), 'anchors': []}
    o = 40
    mo = 40 + 32 * na
    for _ in range(na):
        key, vel, g, sig = struct.unpack('<4f', data[o:o + 16])
        nd, nh, nq, nms = np.frombuffer(data[o + 16:o + 24], '<f2').astype(float)
        first, count, rise = struct.unpack('<HHf', data[o + 24:o + 32])
        modes = []
        for k in range(count):
            p = mo + 10 * (first + k)
            r = struct.unpack('<f', data[p:p + 4])[0]
            a, t = np.frombuffer(data[p + 4:p + 8], '<f2').astype(float)
            modes.append({'ratio': r, 'ampDb': float(a), 't60': float(t), 'shape': data[p + 8]})
        out['anchors'].append({'key': key, 'velocity': vel, 'gainDb': g, 'contact': sig, 'noiseDb': float(nd), 'noiseHz': float(nh),
                               'noiseQ': float(nq), 'noiseMs': float(nms), 'riseMs': rise, 'modes': modes})
        o += 32
    if mo + 10 * nm != len(data):
        raise ValueError('size mismatch')
    return out
