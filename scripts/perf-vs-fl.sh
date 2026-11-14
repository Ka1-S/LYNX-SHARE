#!/usr/bin/env bash
# perf-vs-fl.sh: LYNX vs FL Studio vs Ableton Live on this Windows PC, driven from WSL (round 190gd).
# Every run is one scripts/perf-vs-fl.ps1 launch printing one JSON line, appended to $OUT as
#   {"app":..,"case":..,"rep":..,"load1":..,"r":{...}}
# Summarise with scripts/perf-vs-fl-sum.py $OUT.
#
# usage: perf-vs-fl.sh <phase> [reps]      phases: startup | open | play | disk
#   APPS="lx fl live"  which programs (lx = $LX, lx2 = $LX2 for an A/B of two LYNX builds, interleaved)
#   PROJS="thief shine neverout delay3"  FL demo songs (.flp in $W\proj); "patience" = Live 12's demo set (.als)
#   GATE=8  wait until the WSL 1-min load average is under GATE before each run (up to GATEWAIT s), logged per run
#
# Hygiene: runs are interleaved app by app inside each repetition; each run starts at AboveNormal priority; the
# load average and Windows CPU % at launch are stored with the result. LYNX gets a fresh LYNX_PROFILE_DIR per
# repetition copied from $W\seed (cold = first launch of that profile, warm = the second); LYNX_TEST_MODE=1 (no
# prompts) and LYNX_TEST_MAXIMIZE=1 (maximized like a first start, like FL and Live). The OS file cache is never
# flushed (no elevation): "cold" is first-launch-of-profile, not cold disk.
# Safety (tp-brief): the GUI lock is held for each run only, FL runs also hold the FL lock (GUI first), user-idle
# gate before every launch, no injected input; processes are stopped by their own PID only.
set -u
H=${H:-/mnt/e/lynx-work/perfwin190gd}
W=${W:-'C:\Users\kauan\lynx-test\perfwin190gd'}
WL=${WL:-/mnt/c/Users/kauan/lynx-test/perfwin190gd}
PS=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
FL='C:\Program Files\Image-Line\FL Studio 2026\FL64.exe'
LIVE='C:\ProgramData\Ableton\Live 12 Trial\Program\Ableton Live 12 Trial.exe'
LIVESONG='C:\ProgramData\Ableton\Live 12 Trial\Resources\Core Library\Lessons\Demo Songs\Chuck Sutton - Patience (Live 12 Suite Demo).als'
LX=${LX:-"$W\\lx0127\\LYNX.exe"}
LX2=${LX2:-"$W\\lxnew\\LYNX.exe"}
APPS=${APPS:-"lx fl"}
PROJS=${PROJS:-"thief shine neverout delay3"}
GATE=${GATE:-12}; WINGATE=${WINGATE:-45}; GATEWAIT=${GATEWAIT:-1800}
BENCH="$W\\perf-vs-fl.ps1"; DUP="$W\\tools\\dupmon.exe"
phase=$1; reps=${2:-5}
out=${OUT:-$H/res-$phase.jsonl}
cp "$(dirname "$0")/perf-vs-fl.ps1" "$WL/perf-vs-fl.ps1"
mkdir -p "$WL/prof" "$WL/shots"

wincpu () { $PS -NoProfile -Command "\$v=@(1..2|%{(Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter \"Name='_Total'\").PercentProcessorTime; Start-Sleep -m 500}); [int](\$v|Measure-Object -Average).Average" 2>/dev/null | tr -d '\r'; }
# wait (up to GATEWAIT s) until the WSL load average is under GATE and Windows' CPU under WINGATE %; prints the load
gate () { local i=0 c; while [ $i -lt $((GATEWAIT / 10)) ]; do l=$(cut -d' ' -f1 /proc/loadavg); c=$(wincpu); awk "BEGIN{exit !($l < $GATE && ${c:-100} < $WINGATE)}" && break; sleep 8; i=$((i+1)); done; cut -d' ' -f1 /proc/loadavg; }
idle () { local i; for i in $(seq 1 18); do $PS -NoProfile -ExecutionPolicy Bypass -File 'E:\lynx-work\watch\useridle.ps1' -Min 60 >/dev/null 2>&1 && return 0; sleep 10; done; return 5; }
# one guarded run: GUI lock (+ FL lock for FL), idle gate, then the harness
run () { # app args...
  local app=$1; shift
  (
    exec 8>~/.cache/lynx-wingui.lock; flock -w 3600 8 || { echo '{"error":"no gui lock"}'; exit 0; }
    if [ "$app" = fl ]; then exec 9>~/.cache/lynx-fl.lock; flock -w 3600 9 || { echo '{"error":"no fl lock"}'; exit 0; }
      n=$($PS -NoProfile -Command "@(Get-Process FL64 -ea 0).Count" | tr -d '\r'); [ "$n" = 0 ] || { echo '{"error":"foreign FL64 running"}'; exit 0; }; fi
    if [ "$app" = live ]; then n=$($PS -NoProfile -Command "@(Get-Process 'Ableton Live 12 Trial' -ea 0).Count" | tr -d '\r'); [ "$n" = 0 ] || { echo '{"error":"foreign Live running"}'; exit 0; }; fi
    idle || { echo '{"error":"user active"}'; exit 0; }
    j=$($PS -NoProfile -ExecutionPolicy Bypass -File "$BENCH" -dupmon "$DUP" "$@" </dev/null 2>&1 | tr -d '\r' | grep '^{' | tail -1)
    if [ "$app" = live ]; then # Live: a graceful close (a forced stop makes its next start ask to recover); own PID
      pid=$(echo "$j" | sed -n 's/.*"pid":\([0-9]*\).*/\1/p')
      [ -n "$pid" ] && $PS -NoProfile -ExecutionPolicy Bypass -File 'E:\lynx-work\ableton\tools\finish.ps1' -procId "$pid" -outdir "$W\\shots" -name "live-close" -seconds 45 </dev/null 2>&1 | tr -d '\r' | sed 's/^/# live close: /' >&2
      [ -n "$pid" ] && $PS -NoProfile -Command "if (Get-Process -Id $pid -ea 0) { Stop-Process -Id $pid -Force; 'forced own pid $pid' }" </dev/null 2>&1 | tr -d '\r' | sed 's/^/# live close: /' >&2
    fi
    echo "$j"
  ) 2>>"$H/run-stderr.log"
}
rec () { echo "{\"app\":\"$1\",\"case\":\"$2\",\"rep\":$3,\"load1\":$4,\"r\":${5:-null}}" >> "$out"; echo "$1 $2 #$3 load=$4 :: ${5:0:400}"; }
# keep LYNX's own logs of a run (the next run in the same profile overwrites them): keep <app> <case> <rep> <profile>
keep () { local d="$WL/prof/$4/LYNX" k="$H/logs/$1-$2-$3" log; mkdir -p "$H/logs"; for log in startup.log load.log; do [ -f "$d/$log" ] && cp "$d/$log" "$k-$log"; done; }
newprof () { rm -rf "${WL:?}/prof/${1:?}"; mkdir -p "$WL/prof/$1"; cp -r "$WL/seed/LYNX" "$WL/prof/$1/"; }
lxenv () { echo "LYNX_PROFILE_DIR=$W\\prof\\$1;LYNX_TEST_MODE=1;LYNX_TEST_MAXIMIZE=1;LYNX_TRACE_STARTUP=1"; }
lxexe () { [ "$1" = lx2 ] && echo "$LX2" || echo "$LX"; }
proj () { [ "$1" = patience ] && echo "$LIVESONG" || echo "$W\\proj\\$1.flp"; }
CLOSE="Cloud path warning;Welcome to FL Studio;Invalid project data folder"

case $phase in
disk)
  for d in "$W\\lx0127" "C:\\Program Files\\Image-Line\\FL Studio 2026" "C:\\ProgramData\\Ableton\\Live 12 Trial"; do
    $PS -NoProfile -Command "\$s=(Get-ChildItem -LiteralPath '$d' -Recurse -File -Force -ea 0 | Measure-Object Length -Sum); '{0}|{1} MB|{2} files' -f '$d',[math]::Round(\$s.Sum/1MB),\$s.Count" | tr -d '\r'
  done | tee "$H/disk.txt"; exit 0 ;;
esac

for r in $(seq ${START:-1} $reps); do
for a in $APPS; do
case $phase in
startup)
  case $a in
  lx|lx2) newprof "su-$a-$r"
    for pass in cold warm; do l=$(gate); j=$(run lx -exe "$(lxexe $a)" -envs "$(lxenv su-$a-$r)" -title "LYNX" -shot "$W\\shots\\$a-empty-$pass-$r.bmp"); rec $a "empty-$pass" $r $l "$j"; keep $a "empty-$pass" $r "su-$a-$r"; done ;;
  fl) for pass in cold warm; do l=$(gate); j=$(run fl -exe "$FL" -title "FL Studio" -closeTitles "$CLOSE" -shot "$W\\shots\\fl-empty-$pass-$r.bmp"); rec fl "empty-$pass" $r $l "$j"; done ;;
  live) for pass in cold warm; do l=$(gate); j=$(run live -exe "$LIVE" -title "Ableton Live" -keep 1 -shot "$W\\shots\\live-empty-$pass-$r.bmp"); rec live "empty-$pass" $r $l "$j"; done ;;
  esac ;;
open)
  for p in $PROJS; do f=$(proj $p)
    case $a in
    lx|lx2) newprof "op-$a-$p-$r"
      for pass in cold warm; do l=$(gate); j=$(run lx -exe "$(lxexe $a)" -envs "$(lxenv op-$a-$p-$r)" -argline "\"$f\"" -title "$([ $p = patience ] && echo Patience || echo $p)" -timeoutS 240 -shot "$W\\shots\\$a-$p-$pass-$r.bmp"); rec $a "$p-$pass" $r $l "$j"; keep $a "$p-$pass" $r "op-$a-$p-$r"; done ;;
    fl) [ $p = patience ] && continue
      for pass in cold warm; do l=$(gate); j=$(run fl -exe "$FL" -argline "\"$f\"" -title " - FL Studio" -timeoutS 240 -closeTitles "$CLOSE" -shot "$W\\shots\\fl-$p-$pass-$r.bmp"); rec fl "$p-$pass" $r $l "$j"; done ;;
    live) [ $p = patience ] || continue
      for pass in cold warm; do l=$(gate); j=$(run live -exe "$LIVE" -argline "\"$f\"" -title "Patience" -timeoutS 240 -keep 1 -shot "$W\\shots\\live-$p-$pass-$r.bmp"); rec live "$p-$pass" $r $l "$j"; done ;;
    esac
  done ;;
play)
  for p in $PROJS; do f=$(proj $p)
    case $a in
    lx|lx2) newprof "pl-$a-$p-$r"; l=$(gate); j=$(run lx -mode play -space 1 -exe "$(lxexe $a)" -envs "$(lxenv pl-$a-$p-$r)" -argline "\"$f\"" -title "$([ $p = patience ] && echo Patience || echo $p)" -timeoutS 240 -watch "$W\\shots\\$a-watch-$p-$r.txt"); rec $a "$p@play" $r $l "$j" ;;
    fl) [ $p = patience ] && continue; l=$(gate); j=$(run fl -mode play -space 1 -exe "$FL" -argline "\"$f\"" -title " - FL Studio" -timeoutS 240 -closeTitles "$CLOSE" -watch "$W\\shots\\fl-watch-$p-$r.txt"); rec fl "$p@play" $r $l "$j" ;;
    live) [ $p = patience ] || continue; l=$(gate); j=$(run live -mode play -space 1 -exe "$LIVE" -argline "\"$f\"" -title "Patience" -timeoutS 240 -keep 1 -watch "$W\\shots\\live-watch-$p-$r.txt"); rec live "$p@play" $r $l "$j" ;;
    esac
  done ;;
playwarm) # round 190gn: play with a warm profile (its disk cache filled by a first open: mapped stretched copies)
  for p in $PROJS; do f=$(proj $p)
    case $a in
    lx|lx2) newprof "pw-$a-$p-$r"
      l=$(gate); j=$(run lx -exe "$(lxexe $a)" -envs "$(lxenv pw-$a-$p-$r)" -argline "\"$f\"" -title "$([ $p = patience ] && echo Patience || echo $p)" -timeoutS 240); rec $a "$p-fill" $r $l "$j"
      l=$(gate); j=$(run lx -mode play -space 1 -exe "$(lxexe $a)" -envs "$(lxenv pw-$a-$p-$r)" -argline "\"$f\"" -title "$([ $p = patience ] && echo Patience || echo $p)" -timeoutS 240 -watch "$W\\shots\\$a-watchw-$p-$r.txt"); rec $a "$p@playwarm" $r $l "$j"; keep $a "$p-playwarm" $r "pw-$a-$p-$r" ;;
    esac
  done ;;
deep) # one run per app and project ("empty" = no project): per-thread CPU at idle (play mode without Space), then a
      # VMMap snapshot and, for LYNX, a third run with the message thread sampled (LYNX_PROFILE_MAIN; its sampler thread
      # costs most of a core and its own memory, so that run's CPU / memory numbers are not used)
  VM="$W\\tools\\vmmap\\vmmap64.exe"
  for p in empty $PROJS; do f=$(proj $p); argl=(); [ $p = empty ] || argl=(-argline "\"$f\"")
    case $a in
    lx|lx2) newprof "dp-$a-$p-$r"; l=$(gate)
      j=$(run lx -mode play -space 0 -settleS 10 -exe "$(lxexe $a)" -envs "$(lxenv dp-$a-$p-$r)" "${argl[@]}" -title "$([ $p = empty ] && echo LYNX || ([ $p = patience ] && echo Patience || echo $p))" -timeoutS 240); rec $a "$p@idlethreads" $r $l "$j"
      l=$(gate); j=$(run lx -exe "$(lxexe $a)" -envs "$(lxenv dp-$a-$p-$r);LYNX_PROFILE_MAIN=$W\\shots\\$a-prof-$p-$r.txt" "${argl[@]}" -title "$([ $p = empty ] && echo LYNX || ([ $p = patience ] && echo Patience || echo $p))" -timeoutS 240 -idleS 20); rec $a "$p@profiled" $r $l "$j"
      l=$(gate); j=$(run lx -exe "$(lxexe $a)" -envs "$(lxenv dp-$a-$p-$r)" "${argl[@]}" -title "$([ $p = empty ] && echo LYNX || ([ $p = patience ] && echo Patience || echo $p))" -timeoutS 240 -vmmap "$VM" -vmmapOut "$W\\shots\\$a-vmmap-$p-$r.csv"); rec $a "$p@vmmap" $r $l "$j" ;;
    fl) [ $p = patience ] && continue; l=$(gate)
      j=$(run fl -mode play -space 0 -settleS 10 -exe "$FL" "${argl[@]}" -title "FL Studio" -timeoutS 240 -closeTitles "$CLOSE"); rec fl "$p@idlethreads" $r $l "$j"
      l=$(gate); j=$(run fl -exe "$FL" "${argl[@]}" -title "FL Studio" -timeoutS 240 -closeTitles "$CLOSE" -vmmap "$VM" -vmmapOut "$W\\shots\\fl-vmmap-$p-$r.csv"); rec fl "$p@vmmap" $r $l "$j" ;;
    live) [ $p = empty ] || [ $p = patience ] || continue; l=$(gate)
      j=$(run live -mode play -space 0 -settleS 10 -exe "$LIVE" "${argl[@]}" -title "Ableton Live" -timeoutS 240 -keep 1); rec live "$p@idlethreads" $r $l "$j"
      l=$(gate); j=$(run live -exe "$LIVE" "${argl[@]}" -title "Ableton Live" -timeoutS 240 -keep 1 -vmmap "$VM" -vmmapOut "$W\\shots\\live-vmmap-$p-$r.csv"); rec live "$p@vmmap" $r $l "$j" ;;
    esac
  done ;;
esac
done
done
