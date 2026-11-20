"""Tests of scripts/perf_gate.py (round perfgate190id): python3 -m unittest discover -s tests/perf -p 'test_*.py'."""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "..", "scripts", "perf_gate.py")
sys.path.insert(0, os.path.dirname(SCRIPT))
import perf_gate  # noqa: E402


def write(d, name, text):
    with open(os.path.join(d, name), "w") as f:
        f.write(text)


def bench_line(cpu, rss=100.0, instr=-1, build=10.0):
    return ("GATE scenario=big200 block=256 threads=0 seconds=8.00 cpu_pct=%.3f ns_per_sample=1 instr_per_sample=%d import_ms=2 "
            "build_ms=%.2f rebuild_ms=5 rss_mb=%.1f hash=00ff hash_stable=1\n" % (cpu, instr, build, rss))


def paint_report(ms, instr=None):
    return ("open: 500.0 ms wall, 450.0 ms message thread CPU, working set 300 MB\nmixer, whole frame: median %.3f ms, p90 1 ms%s\nresult=OK\n"
            % (ms, "" if instr is None else ", wall median 1 ms, pixels 00ff, instructions median %d" % instr))


class PerfGateTests(unittest.TestCase):
    def results(self, cpu=50.0, calib=10.0, rss=100.0, paint=2.0, instr=None):
        d = tempfile.mkdtemp()
        write(d, "calib-1-a.txt", "GATE scenario=calibrate calib_ms=%.3f\n" % calib)
        write(d, "bench-big200@256-1.txt", "load 1\n" + bench_line(cpu, rss))
        write(d, "paint-big200-1.txt", paint_report(paint, instr))
        return d

    def baseline(self, d):
        path = os.path.join(d, "baseline.json")
        perf_gate.update(d, path)
        return path

    def test_same_numbers_pass_and_the_metrics_are_normalised(self):
        d = self.results()
        b = self.baseline(d)
        self.assertEqual(perf_gate.compare(d, b), 0)
        m = json.load(open(b))["metrics"]
        self.assertAlmostEqual(m["bench/big200@256/cpu_norm"], 5.0)
        self.assertAlmostEqual(m["bench/big200@256/rss_mb"], 100.0)
        self.assertAlmostEqual(m["paint/big200/mixer/paint_norm"], 0.2)
        self.assertAlmostEqual(m["app/big200/open_rss_mb"], 300.0)
        self.assertNotIn("bench/big200@256/instr_per_sample", m)  # (no CPU counter: -1 is not a value)

    def test_a_slower_machine_is_not_a_regression(self):
        b = self.baseline(self.results())
        d = self.results(cpu=80.0, calib=16.0, paint=3.2)  # everything 60 % slower, the calibration too
        self.assertEqual(perf_gate.compare(d, b), 0)

    def test_a_cpu_regression_fails_and_names_the_cell(self):
        b = self.baseline(self.results())
        d = self.results(cpu=65.0)  # +30 %
        self.assertEqual(perf_gate.compare(d, b), 1)
        self.assertIn("bench:big200@256", open(os.path.join(d, "retry.txt")).read())
        self.assertIn("FAIL", open(os.path.join(d, "summary.md")).read())

    def test_the_best_run_counts(self):
        b = self.baseline(self.results())
        d = self.results(cpu=65.0)
        write(d, "bench-big200@256-2.txt", bench_line(51.0))  # the retry: a busy moment, not a regression
        self.assertEqual(perf_gate.compare(d, b), 0)

    def test_small_absolute_changes_stay_under_the_floor(self):
        b = self.baseline(self.results(rss=20.0))
        d = self.results(rss=28.0)  # +40 % but only 8 MB (floor 12 MB)
        self.assertEqual(perf_gate.compare(d, b), 0)

    def test_a_missing_metric_fails(self):
        b = self.baseline(self.results())
        d = self.results()
        os.remove(os.path.join(d, "paint-big200-1.txt"))
        self.assertEqual(perf_gate.compare(d, b), 1)
        self.assertIn("paint:big200", open(os.path.join(d, "retry.txt")).read())

    def test_a_project_left_out_is_skipped(self):
        d0 = self.results()
        write(d0, "bench-thief@256-1.txt", bench_line(50.0))
        b = self.baseline(d0)
        self.assertEqual(perf_gate.compare(self.results(), b), 0)  # (no thief at all in this run: FL=0)

    def test_baseline_thresholds_override_the_defaults(self):
        b = self.baseline(self.results())
        base = json.load(open(b))
        base["thresholds"] = {"cpu_norm": [0.5, 0.0]}
        json.dump(base, open(b, "w"))
        self.assertEqual(perf_gate.compare(self.results(cpu=65.0), b), 0)

    def test_another_cpu_model_gets_wider_thresholds_and_its_own_numbers_once_added(self):
        b = self.baseline(self.results())
        d = self.results(cpu=65.0)  # +30 %: a regression on this model...
        write(d, "machine.txt", "Linux x86_64, Other CPU, 4 threads, abc\n")
        self.assertEqual(perf_gate.compare(d, b), 0)  # ...within 20 % x 1.5 on another one
        os.environ["PERF_GATE_ADD_MACHINE"] = "1"
        try:
            perf_gate.update(d, b)
        finally:
            del os.environ["PERF_GATE_ADD_MACHINE"]
        base = json.load(open(b))
        self.assertIn("Other CPU", base["machines"])
        self.assertAlmostEqual(base["metrics"]["bench/big200@256/cpu_norm"], 5.0)  # (the default set stays)
        d2 = self.results(cpu=90.0)
        write(d2, "machine.txt", "Linux x86_64, Other CPU, 4 threads, def\n")
        self.assertEqual(perf_gate.compare(d2, b), 1)  # +38 % on its own numbers

    def test_the_command_line(self):
        d = self.results()
        b = os.path.join(d, "b.json")
        self.assertEqual(subprocess.call([sys.executable, SCRIPT, "update", d, b], stdout=subprocess.DEVNULL), 0)
        self.assertEqual(subprocess.call([sys.executable, SCRIPT, "compare", d, b], stdout=subprocess.DEVNULL), 0)

    # rollup 0.1.30 step 9: the paint cells' instruction count (the stable measure)
    def test_paint_instructions_are_collected(self):
        d = self.results(instr=12345678)
        m = perf_gate.collect(d)["metrics"]
        self.assertAlmostEqual(m["paint/big200/mixer/paint_instr"], 12345678.0)
        self.assertAlmostEqual(m["paint/big200/mixer/paint_norm"], 0.2)

    def test_counted_paint_time_alone_does_not_fail(self):
        b = self.baseline(self.results(instr=10000000))
        d = self.results(paint=3.0, instr=10100000)  # time +50 %, the same drawing (+1 % instructions)
        self.assertEqual(perf_gate.compare(d, b), 0)
        self.assertIn("time only", open(os.path.join(d, "summary.md")).read())

    def test_more_paint_instructions_fail(self):
        b = self.baseline(self.results(instr=10000000))
        d = self.results(instr=11000000)  # +10 %
        self.assertEqual(perf_gate.compare(d, b), 1)
        self.assertIn("paint:big200", open(os.path.join(d, "retry.txt")).read())

    def test_paint_time_still_gates_without_a_counted_baseline(self):
        b = self.baseline(self.results())  # (a baseline without instruction counts)
        d = self.results(paint=3.0, instr=10000000)
        self.assertEqual(perf_gate.compare(d, b), 1)

    def test_only_named_metrics_are_added_to_a_cpu_models_baseline(self):
        b = self.baseline(self.results(cpu=50.0))
        d = self.results(cpu=70.0, instr=10000000)
        os.environ["PERF_GATE_ONLY_METRICS"] = "paint_instr"
        try:
            perf_gate.update(d, b)
        finally:
            del os.environ["PERF_GATE_ONLY_METRICS"]
        base = json.load(open(b))
        own = base["machines"][perf_gate.cpu_model(open(os.path.join(d, "machine.txt")).read().strip())] if os.path.exists(os.path.join(d, "machine.txt")) else None
        for numbers in [base["metrics"]] + ([own] if own else []):
            self.assertAlmostEqual(numbers["bench/big200@256/cpu_norm"], 5.0)  # (kept)
            self.assertAlmostEqual(numbers["paint/big200/mixer/paint_instr"], 10000000.0)  # (added)


if __name__ == "__main__":
    unittest.main()
