#!/usr/bin/env bash
# perf-gate.sh: the big-project performance gate (round perfgate190id, community slice B12; docs/RELEASE.md step 2b).
#
# Measures, headless, on heavy projects: the audio engine's CPU per block size (64 / 128 / 256, one thread; also the
# instructions retired where the CPU counters can be read), the project's import / first build / rebuild (an edit's
# new snapshot) and the peak memory (tools/perfbench); with the app: .lynx save and load times (--perfgen), the open
# time and memory and the paint cost per frame of every main view (--paintbench, off screen), and optionally the full
# start-up with the project (--opentime, a window: Xvfb). Then compares with a stored baseline (tests/perf/) and fails
# on any regression past its threshold (scripts/perf_gate.py; cells that fail are measured once more first, and the
# best run counts).
#
# Projects: the synthetic ones of core/PerfProjects.h (synth100, inserts50, stream, big200, automation; generated, the
# same bytes every time) and, with FL=1, FL Studio's demo songs Thief and Exoplanet plus Live's Patience from the
# local installs (never committed). CI runs the synthetic ones (.github/workflows/tests.yml, profile ci-ubuntu).
#
# usage: scripts/perf-gate.sh [compare|update|measure] [results dir]
#   compare (default)  measure, then compare with tests/perf/baseline-$PROFILE.json; exit 1 on a regression
#   update             measure twice and write the baseline (after a reviewed, intended change: commit it with a reason)
#   measure            measure only (results.json)
# env:
#   PROFILE=local         baseline name (local = this machine; ci-ubuntu = GitHub's ubuntu-24.04 runners)
#   PERFBENCH=path        tools/perfbench build (default: build-linux/perfbench, then build/perfbench)
#   LYNX_APP=path         the app (default: build-linux/LYNX_artefacts/Release/LYNX, then build/...); APP=0: skip it
#   OPENTIME=1            also --opentime (opens a window: needs DISPLAY, xvfb-run, or XVFB=<Xvfb binary>)
#   FL=1                  also FL's Thief / Exoplanet and Live's Patience (FL_ROOT, LIVE_DEMO)
#   BLOCKS="64 128 256"   SCENARIOS="synth100 inserts50 stream big200 automation"   SECONDS_AUDIO=4   REPS=3
#   CORES=7-11            pin the measurements (taskset); LOAD_MAX=6 / LOAD_WAIT=1800: wait for a quiet machine (local)
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
mode=${1:-compare}
# (keep OUT on a local disk: the generated projects' audio is read from there, and a network or WSL-mounted Windows
# drive turns the open times into file-system times)
OUT=${2:-$ROOT/build-perfgate/$(date +%Y%m%d-%H%M%S)}
PROFILE=${PROFILE:-local}
BASELINE=${BASELINE:-$ROOT/tests/perf/baseline-$PROFILE.json}
BLOCKS=${BLOCKS:-"64 128 256"}
SCENARIOS=${SCENARIOS:-"synth100 inserts50 stream big200 automation"}
SECONDS_AUDIO=${SECONDS_AUDIO:-4}
REPS=${REPS:-3}
LOAD_MAX=${LOAD_MAX:-}
LOAD_WAIT=${LOAD_WAIT:-1800}
FL_ROOT=${FL_ROOT:-"/mnt/c/Program Files/Image-Line/FL Studio 2026"}
LIVE_DEMO=${LIVE_DEMO:-"/mnt/c/ProgramData/Ableton/Live 12 Trial/Resources/Core Library/Lessons/Demo Songs/Chuck Sutton - Patience (Live 12 Suite Demo).als"}
first () { for f in "$@"; do [ -x "$f" ] && { echo "$f"; return; }; done; }
PERFBENCH=${PERFBENCH:-$(first build-linux/perfbench build/perfbench)}
[ -n "$PERFBENCH" ] && [ -x "$PERFBENCH" ] || { echo "perf-gate: no perfbench (build the 'perfbench' target, or set PERFBENCH)" >&2; exit 2; }
if [ "${APP:-1}" != 0 ]; then LYNX_APP=${LYNX_APP:-$(first build-linux/LYNX_artefacts/Release/LYNX build/LYNX_artefacts/Release/LYNX)}; else LYNX_APP=; fi
mkdir -p "$OUT"
PIN=(); [ -n "${CORES:-}" ] && PIN=(taskset -c "$CORES")

# FL demo songs (local only, never committed)
declare -A FLP=([thief]="$FL_ROOT/Data/Demo projects/Demo songs/Ookay - Thief.flp" [exoplanet]="$FL_ROOT/Data/Demo projects/Demo songs/Eric Kauffmann & NrgMind - Exoplanet.flp")
EXTRA=()
if [ "${FL:-0}" = 1 ]; then
  for k in thief exoplanet; do [ -f "${FLP[$k]}" ] && EXTRA+=("$k") || echo "perf-gate: FL demo $k not found, skipped" >&2; done
  [ -f "$LIVE_DEMO" ] && EXTRA+=(patience) || echo "perf-gate: Live demo not found, skipped" >&2
fi
scenario_arg () { case $1 in thief|exoplanet) echo "flp:${FLP[$1]}";; patience) echo "als:$LIVE_DEMO";; *) echo "$1";; esac; }
project_file () { case $1 in thief|exoplanet) echo "${FLP[$1]}";; patience) echo "$LIVE_DEMO";; *) echo "$OUT/projects/$1.lynx";; esac; }

quiet () { # wait for a quiet machine (local runs: other jobs skew the numbers); logs the load
  [ -z "$LOAD_MAX" ] && return 0
  local t=0
  while awk "BEGIN{exit !($(cut -d' ' -f1 /proc/loadavg) >= $LOAD_MAX)}"; do
    [ $t -ge "$LOAD_WAIT" ] && { echo "perf-gate: load still $(cut -d' ' -f1 /proc/loadavg) after ${LOAD_WAIT}s, measuring anyway" >&2; return 0; }
    sleep 15; t=$((t + 15))
  done
}

# the app's commands: a private Xvfb when there is no display and XVFB names one; else xvfb-run
XPID=
APP_PROFILE=$OUT/profile
app_run () { # report-producing app command (timeout 15 min), in the profile $APP_PROFILE
  local app_env=(env LYNX_TEST_MODE=1 LYNX_PROFILE_DIR="$APP_PROFILE" XDG_CONFIG_HOME="$OUT/xdg")
  if [ -n "${DISPLAY:-}" ]; then timeout 900 "${app_env[@]}" "${PIN[@]}" "$LYNX_APP" "$@"
  elif command -v xvfb-run > /dev/null; then timeout 900 "${app_env[@]}" xvfb-run -a "${PIN[@]}" "$LYNX_APP" "$@"
  else timeout 900 "${app_env[@]}" "${PIN[@]}" "$LYNX_APP" "$@"; fi
}
if [ -n "$LYNX_APP" ] && [ -z "${DISPLAY:-}" ] && [ -n "${XVFB:-}" ]; then
  d=$((90 + RANDOM % 9)); "$XVFB" ":$d" -screen 0 1920x1080x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 & XPID=$!
  export DISPLAY=":$d"; sleep 2
fi
cleanup () { [ -n "$XPID" ] && kill "$XPID" 2> /dev/null; [ -n "${GITHUB_STEP_SUMMARY:-}" ] && [ -f "$OUT/summary.md" ] && cat "$OUT/summary.md" >> "$GITHUB_STEP_SUMMARY"; return 0; }
trap cleanup EXIT

{ echo "$(uname -sm), $(grep -m1 'model name' /proc/cpuinfo 2> /dev/null | cut -d: -f2 | sed 's/^ //'), $(nproc) threads, $(git rev-parse --short HEAD 2> /dev/null)"; } > "$OUT/machine.txt"

calibrate () { quiet; "${PIN[@]}" "$PERFBENCH" calibrate > "$OUT/calib-$1.txt" 2>&1; } # (gated as the cells: the times are divided by it)
bench () { # cell (scenario@block) run
  local s=${1%@*} b=${1#*@} extra=()
  quiet
  echo "load $(cut -d' ' -f1 /proc/loadavg)" > "$OUT/bench-$1-$2.txt"
  timeout 1200 "${PIN[@]}" "$PERFBENCH" "$(scenario_arg "$s")" --block "$b" --seconds "$SECONDS_AUDIO" --reps "$REPS" --gate "${extra[@]}" >> "$OUT/bench-$1-$2.txt" 2>&1 \
    || echo "perf-gate: perfbench $1 failed (exit $?)" | tee -a "$OUT/errors.txt" >&2
}
paint () { # project run
  quiet
  # (a fresh profile each time: a second open would find the first one's stream cache and measure no decoding)
  APP_PROFILE=$OUT/profile-paint-$1-$2 LYNX_PAINTBENCH_FRAMES=${PAINT_FRAMES:-20} app_run --paintbench "$(project_file "$1")" "$OUT/paint-$1-$2.txt" > "$OUT/paint-$1-$2.log" 2>&1
  grep -q '^result=OK' "$OUT/paint-$1-$2.txt" 2> /dev/null || echo "perf-gate: paintbench $1 failed" | tee -a "$OUT/errors.txt" >&2
}
opentime () { # project run (a window)
  quiet
  # (a fresh profile each time, as a first start: a device saved by the run before could be opened differently)
  APP_PROFILE=$OUT/profile-open-$1-$2 app_run --opentime "$(project_file "$1")" "$OUT/open-$1-$2.txt" 3 > "$OUT/open-$1-$2.log" 2>&1
  grep -q '^result=OK' "$OUT/open-$1-$2.txt" 2> /dev/null || echo "perf-gate: opentime $1 failed" | tee -a "$OUT/errors.txt" >&2
}
projects () { local p; for p in $SCENARIOS; do echo "$p"; done; for p in "${EXTRA[@]}"; do echo "$p"; done; }

measure_all () { # run number
  local n=$1 s b p
  calibrate "$n-a"
  for s in $(projects); do for b in $BLOCKS; do echo "== bench $s@$b #$n"; bench "$s@$b" "$n"; done; done
  if [ -n "$LYNX_APP" ]; then
    [ -f "$OUT/perfgen.txt" ] || { echo "== perfgen"; app_run --perfgen "$OUT/projects" "$OUT/perfgen.txt" "$(echo $SCENARIOS | tr ' ' ,)" > "$OUT/perfgen.log" 2>&1; grep -q '^result=OK' "$OUT/perfgen.txt" 2> /dev/null || echo "perf-gate: perfgen failed" | tee -a "$OUT/errors.txt" >&2; }
    for p in $(projects); do echo "== paint $p #$n"; paint "$p" "$n"; done
    if [ "${OPENTIME:-0}" = 1 ]; then for p in $(projects); do echo "== opentime $p #$n"; opentime "$p" "$n"; done; fi
  fi
  calibrate "$n-b"
}

retry_cells () { # run number; the cells perf_gate.py listed
  local n=$1 c
  calibrate "$n-a"
  while read -r c; do
    case $c in
      bench:*) echo "== retry bench ${c#bench:}"; bench "${c#bench:}" "$n" ;;
      paint:*) echo "== retry paint ${c#paint:}"; paint "${c#paint:}" "$n" ;;
      open:*) echo "== retry opentime ${c#open:}"; opentime "${c#open:}" "$n" ;;
    esac
  done < "$OUT/retry.txt"
  calibrate "$n-b"
}

echo "perf-gate: $mode, profile $PROFILE, results in $OUT; perfbench $PERFBENCH; app ${LYNX_APP:-none}; extra: ${EXTRA[*]:-none}"
case $mode in
  measure) measure_all 1; python3 scripts/perf_gate.py collect "$OUT" > /dev/null; echo "perf-gate: $OUT/results.json" ;;
  update) measure_all 1; measure_all 2; mkdir -p "$(dirname "$BASELINE")"; python3 scripts/perf_gate.py update "$OUT" "$BASELINE" ;;
  compare)
    if [ ! -f "$BASELINE" ]; then # (a new profile: nothing to compare with; the run becomes a candidate baseline)
      measure_all 1; python3 scripts/perf_gate.py update "$OUT" "$OUT/baseline-candidate.json" > /dev/null
      echo "::warning::perf-gate: no baseline $BASELINE: nothing compared; candidate in $OUT/baseline-candidate.json (commit it as the baseline after a review)"
      exit 0
    fi
    measure_all 1
    python3 scripts/perf_gate.py compare "$OUT" "$BASELINE" > /dev/null && { cat "$OUT/summary.md"; exit 0; }
    for n in $(seq 2 $((1 + ${RETRIES:-2}))); do # (failing cells again: a busy moment on a shared runner is not a regression)
      echo "perf-gate: measuring again: $(tr '\n' ' ' < "$OUT/retry.txt")"
      retry_cells "$n"
      python3 scripts/perf_gate.py compare "$OUT" "$BASELINE" > /dev/null && { cat "$OUT/summary.md"; exit 0; }
    done
    cat "$OUT/summary.md"; exit 1 ;;
  *) echo "usage: $0 [compare|update|measure] [results dir]" >&2; exit 2 ;;
esac
