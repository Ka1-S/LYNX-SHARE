#!/usr/bin/env python3
"""perf-vs-fl-sum.py <res.jsonl> [field ...]: median [min-max] (n) per case and app (round 190gd).
Default fields: window_s settled_s idle_cpu_pct gpu_pct pws_mb priv_mb ws_mb pws_tree_mb threads play_cpu_pct ui_fps."""
import collections, json, statistics, sys

DEFAULT = "window_s settled_s idle_cpu_pct gpu_pct pws_mb priv_mb ws_mb pws_tree_mb threads gdi play_cpu_pct ui_fps ui_p95_ms".split()
rows = collections.OrderedDict()
for line in open(sys.argv[1]):
    line = line.strip()
    if not line:
        continue
    try:
        d = json.loads(line)
    except Exception:
        continue
    if not isinstance(d.get("r"), dict) or "error" in d["r"]:
        continue
    rows.setdefault((d["case"], d["app"]), []).append(d)
fields = sys.argv[2:] or DEFAULT
for (case, app), rs in sorted(rows.items()):
    out = []
    for f in fields:
        v = []
        for r in rs:
            x = r["r"].get(f)
            try:
                x = float(x)
            except (TypeError, ValueError):
                continue
            if x >= 0:
                v.append(x)
        if v:
            out.append(f"{f}={statistics.median(v):.4g}[{min(v):.4g}-{max(v):.4g}]")
    loads = [float(r["load1"]) for r in rs]
    cpus = [float(r["r"].get("sys_cpu_pct", -1)) for r in rs]
    print(f"{case:18s} {app:5s} n={len(rs)} " + " ".join(out) + f"  load={min(loads):.1f}-{max(loads):.1f} wincpu={min(cpus):.0f}-{max(cpus):.0f}%")
