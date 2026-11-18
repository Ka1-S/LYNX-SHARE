#!/usr/bin/env python3
# Names the LYNX.exe frames of a message-thread profile (LYNX_PROFILE_MAIN, src/platform/CrashGuard.cpp; round 190kz).
#
# The profiler writes LYNX.exe's own frames as raw addresses ("-- raw" section of each window: "S|T <rva> <weight>")
# because loading the ~750 MB LYNX.pdb inside the measured process took longer than a measurement run. This script
# names them afterwards with llvm-symbolizer and the PDB next to the exe (inlined functions included), and prints, for
# the windows chosen, the functions by self weight and by total (self + callees) weight, as shares of all samples.
#
# usage: profile_symbolize.py <profile.txt> <LYNX.exe> [--from S] [--to S] [--top N] [--symbolizer PATH]
#   llvm-symbolizer: PATH, $LLVM_SYMBOLIZER, or llvm-symbolizer on the PATH (LLVM 18 reads PDBs on Linux)
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
from collections import defaultdict


def image_base(exe):
    with open(exe, "rb") as f:
        head = f.read(4096)
    pe = struct.unpack_from("<I", head, 0x3C)[0]
    magic = struct.unpack_from("<H", head, pe + 24)[0]
    return struct.unpack_from("<Q", head, pe + 24 + 24)[0] if magic == 0x20B else struct.unpack_from("<I", head, pe + 24 + 28)[0]


def symbolize(symbolizer, exe, base, rvas):
    """rva -> list of function names, innermost (inlined) first"""
    names = {}
    if not rvas:
        return names
    order = sorted(rvas)
    text = "\n".join("0x%x" % (base + r) for r in order) + "\n"
    out = subprocess.run([symbolizer, "--obj=" + exe, "--functions=short", "--inlining", "--demangle"],
                         input=text, capture_output=True, text=True, check=False).stdout
    # LLVM style: per address, pairs of lines (function, file:line:col), innermost first; a blank line after each address
    blocks = out.split("\n\n")
    for rva, block in zip(order, blocks):
        lines = [l for l in block.split("\n") if l.strip()]
        funcs = [lines[i] for i in range(0, len(lines), 2)]
        names[rva] = [f if f != "??" else "LYNX.exe+0x%x" % rva for f in funcs] or ["LYNX.exe+0x%x" % rva]
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profile")
    ap.add_argument("exe")
    ap.add_argument("--from", dest="t0", type=float, default=0.0)
    ap.add_argument("--to", dest="t1", type=float, default=1e18)
    ap.add_argument("--top", type=int, default=45)
    ap.add_argument("--symbolizer", default=os.environ.get("LLVM_SYMBOLIZER") or shutil.which("llvm-symbolizer") or "llvm-symbolizer")
    a = ap.parse_args()

    windows = 0
    samples = 0
    cpu = []
    self_raw = defaultdict(int)
    total_raw = defaultdict(int)
    other_self = defaultdict(float)   # non-LYNX self lines, in % of their window, weighted back to samples
    other_total = defaultdict(float)
    section = None
    take = False
    wsamples = 0
    for line in open(a.profile, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        m = re.match(r"=== window ([\d.]+)-([\d.]+) s: (\d+) ticks, message thread CPU ([\d.]+) %", line)
        if m:
            take = a.t0 <= float(m.group(1)) and float(m.group(2)) <= a.t1
            section = None
            if take:
                windows += 1
                cpu.append(float(m.group(4)))
            continue
        if not take:
            continue
        if line.startswith("-- raw"):
            section = "raw"
            wsamples = int(re.search(r"of (\d+)", line).group(1))
            samples += wsamples
            continue
        if line.startswith("-- self"):
            section = "self"
            continue
        if line.startswith("-- total"):
            section = "total"
            continue
        if line.startswith("--"):
            section = None
            continue
        if section == "raw":
            kind, rva, w = line.split()
            (self_raw if kind == "S" else total_raw)[int(rva, 16)] += int(w)
        elif section in ("self", "total"):
            m = re.match(r"\s*([\d.]+) %\s+(.*)", line)
            if m and not m.group(2).startswith("LYNX.exe+0x"):
                (other_self if section == "self" else other_total)[m.group(2)] += float(m.group(1))
    if windows == 0:
        sys.exit("no window in the range")
    base = image_base(a.exe)
    # caller frames are return addresses: the call is the instruction before (rva - 1), else the inlined function of the
    # next statement would be named
    names = symbolize(a.symbolizer, a.exe, base, set(self_raw) | {r - 1 for r in total_raw})
    self_fn = defaultdict(int)
    total_fn = defaultdict(int)
    for r, w in self_raw.items():
        self_fn[names.get(r, ["?"])[0]] += w
    for r, w in total_raw.items():
        for f in dict.fromkeys(names.get(r - 1, ["?"])):
            total_fn[f] += w
    pct = lambda w: 100.0 * w / max(1, samples)
    print("%d windows, message thread CPU %.1f %% (mean of the windows), %d weight units" % (windows, sum(cpu) / len(cpu), samples))
    print("-- self (LYNX.exe functions, inlined callees named; shares of all samples)")
    for f, w in sorted(self_fn.items(), key=lambda kv: -kv[1])[: a.top]:
        print("%6.1f %%  %s" % (pct(w), f))
    print("-- self, other modules (mean %% of the windows' samples)")
    for f, v in sorted(other_self.items(), key=lambda kv: -kv[1])[:15]:
        print("%6.1f %%  %s" % (v / windows, f))
    print("-- total (self + callees; LYNX.exe functions)")
    for f, w in sorted(total_fn.items(), key=lambda kv: -kv[1])[: a.top + 25]:
        print("%6.1f %%  %s" % (pct(w), f))


if __name__ == "__main__":
    main()
