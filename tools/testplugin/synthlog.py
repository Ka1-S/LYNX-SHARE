#!/usr/bin/env python3
"""Reads a LYNX Test Synth log (tools/testplugin/lynx_test_synth_vst3.cpp) and reports, per plugin instance:
- play head: process calls while playing whose position is not where the previous block ended (repeats / jumps)
- notes: note ons, note offs, keys left sounding, note ons of a key that already sounds (a doubled voice), and the
  shortest / longest note in samples (counted from the blocks, independent of the reported position)
usage: synthlog.py <log> [bpm] [rate]"""
import sys, collections

def main():
    path = sys.argv[1]
    bpm = float(sys.argv[2]) if len(sys.argv) > 2 else 128.0
    rate = float(sys.argv[3]) if len(sys.argv) > 3 else 44100.0
    qps = bpm / 60.0 / rate  # quarters per sample
    inst = collections.defaultdict(lambda: dict(prev=None, samples=0, blocks=0, playing_blocks=0, repeats=0, jumps=0,
                                                 worst=0.0, on=0, off=0, sounding=collections.Counter(), doubled=0,
                                                 starts={}, lengths=[], modes=collections.Counter(), block_at=0))
    for line in open(path, errors='replace'):
        parts = line.split()
        if not parts or (parts[0].startswith('P') and len(parts) < 6) or (parts[0].startswith('E') and len(parts) < 4):
            continue  # (a line still being written)
        tag = parts[0]
        if tag.startswith('P'):
            d = inst[tag[1:]]
            ppq, n, playing, mode = float(parts[1]), int(parts[2]), parts[3] == '1', parts[4]
            d['blocks'] += 1
            d['block_at'] = d['samples']
            if playing:
                d['playing_blocks'] += 1
                d['modes'][mode] += 1
                prev = d['prev']
                if prev is not None and prev[2]:
                    expected = prev[0] + prev[1] * qps
                    err = abs(ppq - expected)
                    if err > 1e-6:
                        if abs(ppq - prev[0]) < 1e-9 and prev[1] > 0:
                            d['repeats'] += 1
                        elif err > 0.25:
                            d['jumps'] += 1
                        else:
                            d['worst'] = max(d['worst'], err)
            d['prev'] = (ppq, n, playing)
            d['samples'] += n
        elif tag.startswith('E'):
            d = inst[tag[1:]]
            kind, key, off = parts[1], int(parts[2]), int(parts[3])
            at = d['block_at'] + off
            if kind == 'on':
                d['on'] += 1
                if d['sounding'][key] > 0:
                    d['doubled'] += 1
                d['sounding'][key] += 1
                d['starts'].setdefault(key, []).append(at)
            else:
                d['off'] += 1
                if d['sounding'][key] > 0:
                    d['sounding'][key] -= 1
                    if d['starts'].get(key):
                        d['lengths'].append(at - d['starts'][key].pop(0))
    for k, d in sorted(inst.items()):
        left = sum(v for v in d['sounding'].values() if v > 0)
        lengths = sorted(d['lengths'])
        print(f"instance {k}: blocks {d['blocks']} (playing {d['playing_blocks']}, modes {dict(d['modes'])}); "
              f"play head: {d['repeats']} blocks repeat the previous position, {d['jumps']} jumps, worst drift {d['worst']:.2e} q")
        print(f"  notes: {d['on']} on, {d['off']} off, {d['doubled']} doubled (on over a sounding key), {left} left sounding; "
              f"length min {lengths[0] if lengths else '-'} max {lengths[-1] if lengths else '-'} samples, "
              f"{len(lengths)} measured")
        if lengths:
            hist = collections.Counter(lengths)
            print("  most common lengths:", hist.most_common(6))

main()
