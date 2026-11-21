#!/usr/bin/env python3
"""Big-project performance gate: collect, compare, update (round perfgate190id, community slice B12).

scripts/perf-gate.sh runs the measurements into a results folder; this script reads it.

  perf_gate.py collect <results dir>                      -> <dir>/results.json (every metric, best of the runs)
  perf_gate.py compare <results dir> <baseline.json>      -> <dir>/summary.md; exit 1 on a regression, and the
                                                             cells to measure again in <dir>/retry.txt
  perf_gate.py update  <results dir> <baseline.json>      -> writes the baseline from these results (keeps its
                                                             thresholds)
  perf_gate.py ab      <results dir>                      -> <dir>/summary-ab.md: the interleaved A/B of perf-gate.sh ab
                                                             (<dir>/base vs <dir>/new, median vs median); exit 1 on a
                                                             regression

Files in the results folder (written by perf-gate.sh):
  calib-<n>.txt              perfbench calibrate (GATE scenario=calibrate calib_ms=...)
  bench-<cell>-<n>.txt       perfbench <scenario> --gate (one GATE line); <cell> = <scenario>@<block>
  paint-<project>-<n>.txt    LYNX --paintbench report (open line, "<view>, whole frame: median X ms ...")
  open-<project>-<n>.txt     LYNX --opentime report (key=value lines)
  perfgen.txt                LYNX --perfgen report (save_ms / load_ms per project)

Metrics are "lower is better". With several runs of a cell the best (lowest) value counts: noise on a shared machine
only ever adds time. Times are divided by the run's calibration (a fixed piece of work, perfbench calibrate), so a
faster or slower machine moves both alike; memory and instruction counts are compared as they are.

A baseline keeps one set of numbers per CPU model ("machines"; CI runners come with several): a run compares with
its own model's numbers, or with the default set and every threshold widened by cross_machine_factor (1.5). With
PERF_GATE_ADD_MACHINE=1, update only adds this run's CPU model; with PERF_GATE_ONLY_METRICS=paint_instr (names, comma
separated) it only adds those metrics of this run to its model's numbers and keeps every other number.

Paint cells (rollup 0.1.30 step 9): --paintbench also reports, on Linux where the CPU counter can be read, the
instructions the message thread retires per whole frame ("paint_instr"). Drawn by the software renderer, the same pixels
retire nearly the same instructions on every run, while the frame's time divided by the (calculation-bound)
calibration moves with the runner's memory system: one CPU model's frames came out 20 … 50 % "slower" than another's
for the same code. Where the run and the baseline both have a cell's instruction count, the count gates it (tight
threshold) and the time is reported only ("slower (time only)"); elsewhere the time gates, as before.

Wall-clock cells and their instruction counts (rollup 0.1.31 step 5, round 190ok's proposal, ROLLUP031-S4-CELLS.md): one
stored run of a ~5 ms edit or of a CPU share moves +-12 ... 25 % on the same binary minutes apart, so a baseline's lucky
run set bars that later runs missed by 40-100 %. Now (1) a bench cell's cpu_norm is held to its instr_per_sample and the
edit_norm to its edit_instr (perfbench counts the 12 edits' instructions) the way paint_norm is held to paint_instr: when
both sides counted the cell's instructions and the count stayed within its limit, a slower time is reported, not failed
("slower (time only)"); a time failure counts when its instruction cell moved too, or (2) when an interleaved A/B against
main confirms it: `perf-gate.sh ab` runs main's perfbench (BASE_PERFBENCH) and this one alternately, >= 5 rounds
(main, new, new, main, ...), and compares the MEDIANS of each side's runs (`perf_gate.py ab`), every wall-clock cell
included. Cells without an instruction count (build / rebuild / import, save / load, open) gate on time as before.

A metric regresses when it is worse than the baseline by more than its relative threshold AND by more than its
absolute floor (small values are noisy in absolute terms). Missing metrics that the baseline has fail too (a step
that stopped working). Metrics without a baseline are reported as new and never fail.
"""
import json
import os
import re
import sys
from collections import defaultdict

# default thresholds: (relative, absolute floor in the metric's own unit after normalisation)
DEFAULT_THRESHOLDS = {
    "instr_per_sample": [0.04, 50.0],
    "cpu_norm": [0.20, 0.05],          # CPU % of a core / calibration ms
    "build_norm": [0.30, 0.2],
    "rebuild_norm": [0.30, 0.2],
    "edit_norm": [0.30, 0.05],
    "edit_instr": [0.05, 50000.0],      # instructions of one edit's snapshot, median of 12 (rollup 0.1.31 step 5)
    "import_norm": [0.30, 0.2],
    "rss_mb": [0.10, 12.0],
    "paint_norm": [0.30, 0.03],
    "paint_instr": [0.05, 20000.0],     # instructions per whole frame (rollup step 9: the stable paint measure)
    "open_norm": [0.30, 0.2],
    "open_rss_mb": [0.10, 24.0],
    "save_norm": [0.30, 0.2],
    "load_norm": [0.30, 0.2],
    "opentime_load_norm": [0.30, 40.0],
    "opentime_rss_mb": [0.12, 32.0],
    "idle_cpu_pct": [0.50, 1.5],
}

NUM = r"(-?[0-9.]+)"


def kv(line):
    return dict(re.findall(r"(\w+)=(\S+)", line))


def calibration(d):
    vals = []
    for f in sorted(os.listdir(d)):
        if f.startswith("calib-"):
            for line in open(os.path.join(d, f), errors="replace"):
                if line.startswith("GATE "):
                    vals.append(float(kv(line)["calib_ms"]))
    if not vals:
        sys.exit("perf_gate: no calibration in " + d)
    vals.sort()
    return vals[len(vals) // 2], vals


# the instruction cell that holds a wall-clock cell (rollup 0.1.31 step 5: cpu_norm and edit_norm, as paint_norm)
COUNTED_BY = {"cpu_norm": "instr_per_sample", "edit_norm": "edit_instr", "paint_norm": "paint_instr"}


def collect(d, pick=min):
    calib, calibs = calibration(d)
    runs = defaultdict(list)  # metric -> values
    info = {}

    def add(key, value):
        if value is not None and value == value and value >= 0:
            runs[key].append(value)

    for f in sorted(os.listdir(d)):
        p = os.path.join(d, f)
        m = re.match(r"bench-(.+)-\d+\.txt$", f)
        if m:
            cell = m.group(1)
            for line in open(p, errors="replace"):
                if not line.startswith("GATE "):
                    continue
                g = kv(line)
                base = "bench/" + cell + "/"
                add(base + "cpu_norm", float(g["cpu_pct"]) / calib)
                if float(g["instr_per_sample"]) > 0:
                    add(base + "instr_per_sample", float(g["instr_per_sample"]))
                if cell.endswith("@256"):  # (the project's load: once per scenario is enough)
                    add(base + "build_norm", float(g["build_ms"]) / calib)
                    add(base + "rebuild_norm", float(g["rebuild_ms"]) / calib)
                    if "edit_ms" in g:
                        add(base + "edit_norm", float(g["edit_ms"]) / calib)
                    if float(g.get("edit_instr", -1)) > 0:
                        add(base + "edit_instr", float(g["edit_instr"]))
                    add(base + "import_norm", float(g["import_ms"]) / calib)
                    add(base + "rss_mb", float(g["rss_mb"]))
                info["hash/" + cell] = g["hash"]
            continue
        m = re.match(r"paint-(.+)-\d+\.txt$", f)
        if m:
            proj = m.group(1)
            text = open(p, errors="replace").read()
            if "result=OK" not in text:
                continue
            om = re.search(r"^open: " + NUM + r" ms wall, " + NUM + r" ms message thread CPU, working set " + NUM + " MB", text, re.M)
            if om:
                add("app/" + proj + "/open_norm", float(om.group(1)) / calib)
                add("app/" + proj + "/open_rss_mb", float(om.group(3)))
            for vm in re.finditer(r"^([a-z ]+), whole frame: median " + NUM + r" ms(.*)$", text, re.M):
                add("paint/" + proj + "/" + vm.group(1).replace(" ", "_") + "/paint_norm", float(vm.group(2)) / calib)
                im = re.search(r", instructions median ([0-9]+)", vm.group(3))
                if im:
                    add("paint/" + proj + "/" + vm.group(1).replace(" ", "_") + "/paint_instr", float(im.group(1)))
            continue
        m = re.match(r"open-(.+)-\d+\.txt$", f)
        if m:
            proj = m.group(1)
            g = dict(line.strip().split("=", 1) for line in open(p, errors="replace") if "=" in line)
            if g.get("result") != "OK":
                continue
            # (from the process' start to the project loaded: the start-up file is opened before the window's probe
            # starts, so "loaded - window" would miss a slow open)
            add("opentime/" + proj + "/opentime_load_norm", 1000.0 * float(g["loaded_s"]) / calib)
            add("opentime/" + proj + "/opentime_rss_mb", float(g["working_set_mb"]))
            add("opentime/" + proj + "/idle_cpu_pct", float(g["idle_cpu_pct"]))
            continue
        if f == "perfgen.txt":
            for line in open(p, errors="replace"):
                pm = re.match(r"(\w+): (.*)", line)
                if pm and "save_ms=" in line:
                    g = kv(pm.group(2))
                    add("project/" + pm.group(1) + "/save_norm", float(g["save_ms"]) / calib)
                    add("project/" + pm.group(1) + "/load_norm", float(g["load_ms"]) / calib)
                    info["lynx_hash/" + pm.group(1)] = g.get("text_hash", "")
    metrics = {k: pick(v) for k, v in runs.items()}
    return {"calib_ms": calib, "calib_runs": calibs, "metrics": metrics, "runs": dict(runs), "info": info,
            "machine": open(os.path.join(d, "machine.txt")).read().strip() if os.path.exists(os.path.join(d, "machine.txt")) else ""}


def threshold_for(key, thresholds):
    name = key.rsplit("/", 1)[1]
    # a baseline may override per metric name or per full key
    return thresholds.get(key) or thresholds.get(name) or DEFAULT_THRESHOLDS.get(name, [0.25, 0.0])


def cpu_model(machine):
    # "Linux x86_64, AMD EPYC 9V45 96-Core Processor, 4 threads, 0d442d6" -> the CPU model
    parts = machine.split(", ")
    return parts[1] if len(parts) > 2 else machine


def cell_of(key):
    parts = key.split("/")
    if parts[0] == "bench":
        return "bench:" + parts[1]
    if parts[0] in ("paint", "app"):
        return "paint:" + parts[1]
    if parts[0] == "opentime":
        return "open:" + parts[1]
    return "perfgen"


def fmt(v):
    return ("%.4g" % v) if abs(v) < 1000 else "%.0f" % v


def compare(d, baseline_path, show=False):
    res = collect(d)
    json.dump(res, open(os.path.join(d, "results.json"), "w"), indent=1, sort_keys=True)
    base = json.load(open(baseline_path))
    thresholds = dict(DEFAULT_THRESHOLDS)
    thresholds.update(base.get("thresholds", {}))
    # a baseline measured on this CPU model compares as it is; another model's (CI runners differ) with the thresholds
    # widened by cross_machine_factor
    model = cpu_model(res["machine"])
    own = base.get("machines", {}).get(model)
    bm, cm = (own if own else base.get("metrics", {})), res["metrics"]
    factor = 1.0 if own else float(base.get("cross_machine_factor", 1.5))
    rows, failed, retry = [], [], set()
    project_of = lambda key: key.split("/")[1].split("@")[0]
    projects_run = {project_of(k) for k in cm}
    for key in sorted(set(bm) | set(cm)):
        rel, floor = threshold_for(key, thresholds)
        rel *= factor
        if key not in cm and project_of(key) not in projects_run:  # (a project this run left out: FL=0, no FL install)
            rows.append((key, fmt(bm[key]), "not run", "", "skipped"))
            continue
        if key not in cm and key.endswith("/instr_per_sample"):  # (the CPU counter is not always readable in a VM)
            rows.append((key, fmt(bm[key]), "n/a", "", "not measured (no CPU counter)"))
            continue
        if key not in cm:
            rows.append((key, fmt(bm[key]), "missing", "", "FAIL (missing)"))
            failed.append(key)
            retry.add(cell_of(key))
            continue
        if key not in bm:
            rows.append((key, "", fmt(cm[key]), "", "new"))
            continue
        b, c = bm[key], cm[key]
        delta = (c - b) / b if b > 0 else 0.0
        worse = c > b * (1.0 + rel) and c - b > floor
        verdict = "FAIL" if worse else ("better" if c < b * (1.0 - rel) and b - c > floor else "ok")
        # (a wall-clock cell whose instructions both sides counted: the count gates it, its time is reported only; a paint
        # cell always so since rollup 0.1.30 step 9, a CPU share or an edit while its count stayed in its limit: step 5)
        name = key.rsplit("/", 1)[1]
        counted = key[: -len(name)] + COUNTED_BY.get(name, "")
        if name in COUNTED_BY and counted in bm and counted in cm and worse:
            crel, cfloor = threshold_for(counted, thresholds)
            count_moved = cm[counted] > bm[counted] * (1.0 + crel * factor) and cm[counted] - bm[counted] > cfloor
            if name == "paint_norm" or not count_moved:
                worse, verdict = False, "slower (time only: the instruction count gates this cell; perf-gate.sh ab to confirm)"
        if worse:
            failed.append(key)
            retry.add(cell_of(key))
        rows.append((key, fmt(b), fmt(c), "%+.1f%%" % (100.0 * delta), verdict + " (limit +%.0f%%)" % (100 * rel)))
    out = ["# Performance gate: %s" % ("FAILED" if failed else "passed"), "",
           ("Compared with this CPU model's own baseline (%s)." % model) if own else
           ("No baseline for this CPU model (%s): the default one, thresholds × %.1f." % (model, factor)), "",
           "Baseline `%s` (%s, calibration %.2f ms); this run: %s, calibration %.2f ms (runs %s)." % (
               os.path.basename(baseline_path), base.get("machine", "?"), base.get("calib_ms", 0), res["machine"], res["calib_ms"],
               ", ".join("%.2f" % x for x in res["calib_runs"])), "",
           "Times are divided by the calibration (\\_norm); lower is better; best of the runs.", "",
           "| metric | baseline | now | change | verdict |", "|---|---|---|---|---|"]
    out += ["| %s |" % " | ".join(r) for r in rows]
    if failed:
        out += ["", "**Regressed:** " + ", ".join(failed)]
    changed = [k for k, v in res["info"].items() if k.startswith("hash/") and base.get("info", {}).get(k) not in (None, v)]
    if changed:
        out += ["", "Output hash changed (not a performance failure; a sound change must be intended): " + ", ".join(changed)]
    text = "\n".join(out) + "\n"
    open(os.path.join(d, "summary.md"), "w").write(text)
    open(os.path.join(d, "retry.txt"), "w").write("\n".join(sorted(retry)) + ("\n" if retry else ""))
    if show:
        print(text)
    return 1 if failed else 0


def update(d, baseline_path, show=False):
    res = collect(d)
    json.dump(res, open(os.path.join(d, "results.json"), "w"), indent=1, sort_keys=True)
    old = json.load(open(baseline_path)) if os.path.exists(baseline_path) else {}
    only = [n for n in os.environ.get("PERF_GATE_ONLY_METRICS", "").split(",") if n]
    if only and old:
        # (rollup step 9) PERF_GATE_ONLY_METRICS=paint_instr,...: only those metrics of this run go into its CPU model's
        # numbers (and the default set when it is that model's or the model has none): every other number stays as it was
        new = dict(old)
        model = cpu_model(res["machine"])
        picked = {k: v for k, v in res["metrics"].items() if k.rsplit("/", 1)[1] in only}
        machines = {m: dict(v) for m, v in old.get("machines", {}).items()}
        if model in machines:
            machines[model].update(picked)
        if model not in machines or cpu_model(old.get("machine", "")) == model:
            new["metrics"] = dict(old.get("metrics", {}), **picked)
        new["machines"] = machines
        json.dump(new, open(baseline_path, "w"), indent=1, sort_keys=True)
        open(baseline_path, "a").write("\n")
        if show:
            print("perf_gate: added %d %s metrics of %s to %s" % (len(picked), ",".join(only), model, baseline_path))
        return 0
    machines = dict(old.get("machines", {}))
    machines[cpu_model(res["machine"])] = res["metrics"]
    keep_default = os.environ.get("PERF_GATE_ADD_MACHINE") == "1" and old.get("metrics")  # (only add this CPU model)
    new = {"profile": old.get("profile", os.path.splitext(os.path.basename(baseline_path))[0]),
           "machine": old["machine"] if keep_default else res["machine"], "calib_ms": old["calib_ms"] if keep_default else res["calib_ms"],
           "metrics": old["metrics"] if keep_default else res["metrics"], "info": res["info"],
           "machines": machines, "thresholds": old.get("thresholds", {})}
    for k in ("note", "cross_machine_factor"):
        if k in old:
            new[k] = old[k]
    json.dump(new, open(baseline_path, "w"), indent=1, sort_keys=True)
    open(baseline_path, "a").write("\n")
    if show:
        print("perf_gate: wrote %s (%d metrics)" % (baseline_path, len(res["metrics"])))
    return 0


def median(v):
    v = sorted(v)
    n = len(v)
    return v[n // 2] if n % 2 else 0.5 * (v[n // 2 - 1] + v[n // 2])


def ab(d, show=False):
    """Rollup 0.1.31 step 5 (round 190ok's proposal): main (<d>/base) and this build (<d>/new) measured alternately in one
    session by perf-gate.sh ab; each metric is the median of its side's runs and the new one fails past the metric's
    threshold, every wall-clock cell included (this comparison is the A/B that confirms a time failure)."""
    b, n = collect(os.path.join(d, "base"), pick=median), collect(os.path.join(d, "new"), pick=median)
    thresholds = dict(DEFAULT_THRESHOLDS)
    rows, failed = [], []
    for key in sorted(set(b["metrics"]) | set(n["metrics"])):
        if key not in b["metrics"] or key not in n["metrics"]:
            rows.append((key, fmt(b["metrics"].get(key, -1)), fmt(n["metrics"].get(key, -1)), "", "one side only"))
            continue
        bv, nv = b["metrics"][key], n["metrics"][key]
        rel, floor = threshold_for(key, thresholds)
        worse = nv > bv * (1.0 + rel) and nv - bv > floor
        runs = "%d / %d" % (len(b["runs"].get(key, [])), len(n["runs"].get(key, [])))
        if worse:
            failed.append(key)
        verdict = "FAIL" if worse else ("better" if nv < bv * (1.0 - rel) and bv - nv > floor else "ok")
        rows.append((key, fmt(bv), fmt(nv), "%+.1f%%" % (100.0 * (nv - bv) / bv if bv > 0 else 0.0), "%s (limit +%.0f%%, runs %s)" % (verdict, 100 * rel, runs)))
    out = ["# Performance A/B (interleaved): %s" % ("FAILED" if failed else "passed"), "",
           "main: %s, calibration median %.2f ms (runs %s)" % (b["machine"], b["calib_ms"], ", ".join("%.2f" % x for x in b["calib_runs"])),
           "new: %s, calibration median %.2f ms (runs %s)" % (n["machine"], n["calib_ms"], ", ".join("%.2f" % x for x in n["calib_runs"])), "",
           "Median of each side's runs; times divided by the side's median calibration; lower is better.", "",
           "| metric | main | new | change | verdict |", "|---|---|---|---|---|"]
    out += ["| %s |" % " | ".join(r) for r in rows]
    if failed:
        out += ["", "**Regressed (A/B):** " + ", ".join(failed)]
    text = "\n".join(out) + "\n"
    open(os.path.join(d, "summary-ab.md"), "w").write(text)
    if show:
        print(text)
    return 1 if failed else 0


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in ("collect", "compare", "update", "ab"):
        sys.exit(__doc__)
    if sys.argv[1] == "ab":
        sys.exit(ab(sys.argv[2], show=True))
    if sys.argv[1] == "collect":
        r = collect(sys.argv[2])
        json.dump(r, open(os.path.join(sys.argv[2], "results.json"), "w"), indent=1, sort_keys=True)
        print(json.dumps(r["metrics"], indent=1, sort_keys=True))
        sys.exit(0)
    sys.exit((compare if sys.argv[1] == "compare" else update)(sys.argv[2], sys.argv[3], show=True))
