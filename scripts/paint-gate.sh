#!/usr/bin/env bash
# paint-gate.sh: the paint gate of a UI change (round 190pb, after the rollup 0.1.31 step-5 regression: a design round
# gated on Thief only, and Live's Patience then drew its Channel rack +44 % and its Clip-launch arrangement +72 %
# instructions a frame). Two LYNX builds, before and after, each `--paintbench` on FL Studio's Thief AND Live's Patience
# (an .flp and an .als: the FL and the Live paths of every view), run alternately; per project, view and frame kind the
# lowest instruction median of the runs is compared. Fails when a cell got more than GATE % dearer, or when a whole
# frame's pixels changed (PIXELS=any accepts a change that was meant: a design change). Linux (the instruction counter).
#
# usage: scripts/paint-gate.sh <LYNX before> <LYNX after> [results dir]
# env:
#   PROJECTS="thief patience"   which projects (also: exoplanet, or a path to an .flp / .als / .lynx)
#   RUNS=2                      runs per build and project (alternating before / after)
#   GATE=1                      % allowed per cell (perf_gate.py holds main to 5 %; a single change gets 1 %)
#   PIXELS=same|any             whole-frame pixel hashes must match (default) or may differ
#   FRAMES=20                   frames per measurement (LYNX_PAINTBENCH_FRAMES)
#   VIEWS="channel rack,..."    only these views (LYNX_PAINTBENCH_VIEWS)
#   FL_ROOT, LIVE_DEMO          as scripts/perf-gate.sh
# Needs a display for the app (DISPLAY, e.g. a private Xvfb); keep the results dir on a local disk.
set -u
[ $# -ge 2 ] || { sed -n 2,20p "$0" >&2; exit 2; }
BEFORE=$1; AFTER=$2
OUT=${3:-$(cd "$(dirname "$0")/.." && pwd)/build-paintgate/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$OUT" || exit 2
FL_ROOT=${FL_ROOT:-"/mnt/c/Program Files/Image-Line/FL Studio 2026"}
LIVE_DEMO=${LIVE_DEMO:-"/mnt/c/ProgramData/Ableton/Live 12 Trial/Resources/Core Library/Lessons/Demo Songs/Chuck Sutton - Patience (Live 12 Suite Demo).als"}
PROJECTS=${PROJECTS:-"thief patience"}
RUNS=${RUNS:-2}; GATE=${GATE:-1}; PIXELS=${PIXELS:-same}
project_file () {
  case $1 in
    thief) echo "$FL_ROOT/Data/Demo projects/Demo songs/Ookay - Thief.flp" ;;
    exoplanet) echo "$FL_ROOT/Data/Demo projects/Demo songs/Eric Kauffmann & NrgMind - Exoplanet.flp" ;;
    patience) echo "$LIVE_DEMO" ;;
    *) echo "$1" ;;
  esac
}
for b in "$BEFORE" "$AFTER"; do [ -x "$b" ] || { echo "paint-gate: not an executable: $b" >&2; exit 2; }; done
for p in $PROJECTS; do [ -f "$(project_file "$p")" ] || { echo "paint-gate: project $p not found: $(project_file "$p")" >&2; exit 2; }; done
for run in $(seq 1 "$RUNS"); do
  for p in $PROJECTS; do
    sides="before after"; [ $((run % 2)) = 0 ] && sides="after before"
    for side in $sides; do
      app=$BEFORE; [ "$side" = after ] && app=$AFTER
      name=$(basename "$p" | tr -c 'A-Za-z0-9\n' '_')
      prof=$OUT/profile-$side-$name-$run; rm -rf "$prof"   # (a fresh profile: no stream cache of an earlier open)
      views=(); [ -n "${VIEWS:-}" ] && views=(LYNX_PAINTBENCH_VIEWS="$VIEWS")
      env LYNX_TEST_MODE=1 LYNX_PROFILE_DIR="$prof" XDG_CONFIG_HOME="$OUT/xdg-$side" LYNX_PAINTBENCH_FRAMES="${FRAMES:-20}" \
        "${views[@]}" timeout 1500 "$app" --paintbench "$(project_file "$p")" "$OUT/paint-$side-$name-$run.txt" \
        > "$OUT/paint-$side-$name-$run.log" 2>&1
      grep -q '^result=OK' "$OUT/paint-$side-$name-$run.txt" 2> /dev/null || echo "paint-gate: $side $p run $run failed (see $OUT/paint-$side-$name-$run.log)" >&2
      rm -rf "$prof"
    done
  done
done
python3 - "$OUT" "$GATE" "$PIXELS" <<'EOF'
import glob, os, re, sys
out, gate, pixels = sys.argv[1], float(sys.argv[2]), sys.argv[3]
def load(side):
    cells, hashes = {}, {}
    for f in glob.glob(os.path.join(out, f"paint-{side}-*.txt")):
        project = re.sub(r"^paint-" + side + r"-(.*)-\d+\.txt$", r"\1", os.path.basename(f))
        for line in open(f, encoding="utf-8", errors="replace"):
            m = re.match(r"(.+?), (whole frame|cached frame|playhead column): .*instructions median (\d+)", line)
            if m:
                k = (project, m.group(1), m.group(2)); v = int(m.group(3))
                cells[k] = min(cells.get(k, v), v)
            m = re.match(r"(.+?), whole frame: .*pixels ([0-9a-f]+)", line)
            if m:
                hashes.setdefault((project, m.group(1)), set()).add(m.group(2))
    return cells, hashes
b, bh = load("before")
a, ah = load("after")
if not b or not a:
    print("paint-gate: no instruction counts (Linux only, and the runs must succeed)"); sys.exit(2)
worst, failed, lines = -100.0, [], []
lines.append(f"| project | view | frame | before | after | change | |\n|---|---|---|---|---|---|---|")
for k in sorted(b):
    if k not in a:
        failed.append(f"{k} missing after"); continue
    d = (a[k] - b[k]) * 100.0 / b[k]; worst = max(worst, d)
    bad = d > gate
    if bad: failed.append(f"{' / '.join(k)} {d:+.2f}%")
    lines.append(f"| {k[0]} | {k[1]} | {k[2]} | {b[k]} | {a[k]} | {d:+.2f}% | {'FAIL' if bad else ''} |")
for k in sorted(bh):
    same = bh[k] == ah.get(k)
    lines.append(f"pixels {k[0]} / {k[1]}: {'same' if same else 'CHANGED ' + ','.join(sorted(bh[k])) + ' -> ' + ','.join(sorted(ah.get(k, {'missing'})))}")
    if not same and pixels != "any": failed.append(f"pixels {' / '.join(k)}")
lines.append(f"worst {worst:+.2f}% (gate +{gate:g} %); {'PASS' if not failed else 'FAIL: ' + '; '.join(failed)}")
open(os.path.join(out, "summary-paint.md"), "w").write("\n".join(lines) + "\n")
print("\n".join(lines))
sys.exit(1 if failed else 0)
EOF
