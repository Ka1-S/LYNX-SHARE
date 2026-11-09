#!/usr/bin/env bash
# usage: scripts/run-tests.sh [-j N] [-s M] <lynx_tests> [lynx_tests arguments: FL Data folder, --filter ...]
#
# Runs the core tests in parallel (round speed185a): the tests are split into M shards (lynx_tests --shard k/M, default
# M = 3 x N), N of them run at a time, then the timing-sensitive tests listed in tests/serial-tests.txt run alone, on an
# idle machine, as they always did. Every test runs exactly once (the shards are a partition) and checks the same.
#
# The output is each shard's output one after the other (test names, their "  FAILED file:line" lines and times), then
# the slowest tests, then ONE "N checks, M failures" line for the whole run: the same format as a single lynx_tests run,
# so the scripts that read it work unchanged. A shard that crashes adds a "  FAILED" line under the test it was running.
# Exit status: 0 only if every shard ran to its end with 0 failures. -j 1: the plain single run of lynx_tests.
# The shard logs stay in <lynx_tests>.shards/ (LYNX_TEST_LOGS=<dir> to choose).
set -u
jobs="${LYNX_TEST_JOBS:-}" shards=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -j) jobs="$2"; shift 2 ;;
        -j*) jobs="${1#-j}"; shift ;;
        -s) shards="$2"; shift 2 ;;
        *) break ;;
    esac
done
bin="${1:?usage: run-tests.sh [-j N] [-s M] <lynx_tests> [arguments]}"; shift
[[ -z "$jobs" || "$jobs" == auto ]] && jobs="$(nproc 2> /dev/null || sysctl -n hw.ncpu 2> /dev/null || echo 4)"
case "$jobs" in '' | *[!0-9]* | 0) echo "run-tests: -j wants a number or auto, not '$jobs'" >&2; exit 2 ;; esac
(( jobs == 1 )) && exec "$bin" "$@"
[[ -n "$shards" ]] || shards=$((jobs * 3))
here="$(cd "$(dirname "$0")/.." && pwd)"
serial="$(grep -v '^ *#' "$here/tests/serial-tests.txt" 2> /dev/null | tr -d ' \r' | grep . | paste -sd, -)"
logs="${LYNX_TEST_LOGS:-$bin.shards}"
mkdir -p "$logs" && rm -f "$logs"/*.log "$logs"/*.times "$logs"/*.rc
weights=()
[[ -f "$here/tests/test-weights.txt" ]] && weights=(--weights "$here/tests/test-weights.txt")
started=$(date +%s)
echo "run-tests: $shards shards, $jobs at a time$([[ -n "$serial" ]] && echo ", then the serial tests alone") (logs in $logs)"

# one shard: <k> [arguments]; writes <k>.log, <k>.times and <k>.rc (xargs runs N at a time; works with macOS' bash 3.2)
export bin logs shards serial
runshard() {
    local k="$1"; shift
    local ex=(); [[ -n "$serial" ]] && ex=(--exclude "$serial")
    "$bin" "$@" ${ex[@]+"${ex[@]}"} --shard "$k/$shards" --times "$logs/$k.times" > "$logs/$k.log" 2>&1
    echo $? > "$logs/$k.rc"
}
export -f runshard
for ((k = 1; k <= shards; ++k)); do echo "$k"; done |
    xargs -P "$jobs" -I{} bash -c 'runshard "$@"' runshard {} "$@" ${weights[@]+"${weights[@]}"}
para=$(( $(date +%s) - started ))
if [[ -n "$serial" ]]; then
    "$bin" "$@" --filter "$serial" --times "$logs/serial.times" > "$logs/serial.log" 2>&1
    echo $? > "$logs/serial.rc"
fi

# the shards' output in order, without each one's own summary; then the totals
checks=0 failures=0 bad=0
parts=(); for ((k = 1; k <= shards; ++k)); do parts+=("$k"); done; [[ -n "$serial" ]] && parts+=(serial)
for p in "${parts[@]}"; do
    f="$logs/$p.log" rc="$(cat "$logs/$p.rc" 2> /dev/null || echo none)"
    sum="$(grep -E '^[0-9]+ checks, [0-9]+ failures$' "$f" 2> /dev/null | tail -1)"
    # everything before the shard's summary block (the blank line before "  N tests in" or before "N checks")
    awk '/^  [0-9]+ tests in .*slowest:$/ || /^[0-9]+ checks, [0-9]+ failures$/ { exit } { buf[n++] = $0 }
         END { while (n > 0 && buf[n - 1] == "") --n; for (i = 0; i < n; ++i) print buf[i] }' "$f" 2> /dev/null
    if [[ -n "$sum" ]]; then
        checks=$((checks + ${sum%% checks*}))
        f2="${sum#*, }"; failures=$((failures + ${f2%% failures}))
    fi
    if [[ -z "$sum" || ( "$rc" != 0 && "$rc" != 1 ) ]]; then
        echo "  FAILED $f:0: shard $p of lynx_tests stopped (exit $rc) during the test above"
        failures=$((failures + 1)); bad=1
    elif [[ "$rc" != 0 && "${sum#*, }" == "0 failures" ]]; then
        bad=1
    fi
done
total=$(( $(date +%s) - started ))
echo
cat "$logs"/*.times 2> /dev/null | sort -k2,2nr | head -15 | awk '{ printf "  %8.1f s  %s\n", $2 / 1000.0, $1 }' > "$logs/slowest.txt"
ntests=$(cat "$logs"/*.times 2> /dev/null | wc -l | tr -d ' ')
echo "  $ntests tests in $total s wall ($para s in $shards shards, $jobs at a time, $((total - para)) s serial); slowest:"
cat "$logs/slowest.txt"
echo
echo "$checks checks, $failures failures"
[[ $failures == 0 && $bad == 0 ]]
