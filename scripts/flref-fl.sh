#!/usr/bin/env bash
# flref-fl.sh — renders FL Studio projects with FL itself (command line: FL64.exe /R /Ewav /O<folder> <project>)
# into a reference cache, one FL instance at a time. FL output is never committed.
#
# usage: flref-fl.sh <list-of-flp-paths> [cache-dir]
#   list: one Linux path per line (/mnt/c/...); the cache keeps <cache>/fl/<group>/<name>.wav, where <group> is the
#   project's folder relative to FL's Data dir with spaces replaced by '_'. Existing renders are kept (delete to redo).
# env: FL_ROOT (default: /mnt/c/Program Files/Image-Line/FL Studio 2026), FLREF_TIMEOUT (seconds per project, 900)
set -u
LIST="$1"
CACHE="${2:-/mnt/c/Users/kauan/lynx-test/flref}"
FL_ROOT="${FL_ROOT:-/mnt/c/Program Files/Image-Line/FL Studio 2026}"
TMO="${FLREF_TIMEOUT:-900}"
LOG="$CACHE/fl/render.log"
mkdir -p "$CACHE/fl"

flRunning () { tasklist.exe /FI "IMAGENAME eq FL64.exe" /NH < /dev/null 2> /dev/null | grep -qi 'FL64.exe'; }
# the Windows PID of the FL64.exe running now (this script starts FL only when no other one runs)
flPid () { tasklist.exe /FI "IMAGENAME eq FL64.exe" /FO CSV /NH < /dev/null 2> /dev/null | tr -d '\r' | awk -F'","' 'NF>1{print $2; exit}'; }
pause1 () { timeout 1 tail -f /dev/null; } # (no sleep builtin needed)

while IFS= read -r flp <&3; do
    [[ -z "$flp" ]] && continue
    rel="${flp#"$FL_ROOT/Data/"}"
    group=$(dirname "$rel" | tr ' /' '_-')
    name=$(basename "$flp" .flp)
    out="$CACHE/fl/$group"
    mkdir -p "$out"
    [[ -s "$out/$name.wav" ]] && continue

    # another FL (e.g. an agent driving FL's UI) — wait for it, up to 30 minutes, then give up
    waited=0
    while flRunning; do
        (( waited == 0 )) && echo "$(date +%T) waiting: FL64.exe already running" >> "$LOG"
        (( waited++ > 1800 )) && { echo "$(date +%T) ABORT: another FL64.exe kept running" >> "$LOG"; exit 3; }
        pause1
    done

    start=$(date +%s.%N)
    "$FL_ROOT/FL64.exe" /R /Ewav "/O$(wslpath -w "$out")" "$(wslpath -w "$flp")" < /dev/null > /dev/null 2>&1 &
    # the launcher may return before FL exits: wait for FL64.exe to appear, then to disappear
    for _ in $(seq 1 20); do flRunning && break; pause1; done
    pid=$(flPid)
    status=ok
    while flRunning; do
        if (( $(date +%s) - ${start%.*} > TMO )); then
            [[ -n "$pid" ]] && taskkill.exe /F /PID "$pid" < /dev/null > /dev/null 2>&1; status=timeout; pause1; rm -f "$out/$name.wav"; break
        fi
        pause1
    done
    wait
    secs=$(awk -v a="$start" -v b="$(date +%s.%N)" 'BEGIN{print b-a}')
    [[ -s "$out/$name.wav" ]] || status="$status-nowav"
    printf '%s\t%s\t%.1f\t%s\n' "$(date +%T)" "$status" "$secs" "$group/$name" >> "$LOG"
done 3< "$LIST"
