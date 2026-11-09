#!/usr/bin/env bash
# Checks an unpacked LYNX plugin download (scripts/package-plugins.sh) the way hosts will load it (round 188b,
# docs/PLUGIN_EXPORT.md), on Windows (Git Bash), Linux or macOS:
#   plugexporttest  the CLAP and each VST3: scan, null test against the engine, automation, state, editor, memory, CPU
#   pluginval       each VST3, strictness 10, no GUI tests (no screen on CI)
#   clap-validator  the CLAP
# Every report goes to <reports dir>; with [pictures], plugexporttest also draws each editor there (PNG).
# Exit 0 = everything passed. A failed tool doesn't stop the others: the summary lists every result.
#
#   scripts/check-plugins.sh <package dir> <plugexporttest> <pluginval> <clap-validator> <reports dir> [pictures]
set -uo pipefail
pkg="$1" tester="$2" pluginval="$3" validator="$4" reports="$5" pictures="${6:-}"
mkdir -p "$reports"
export LYNX_PROFILE_DIR="${LYNX_PROFILE_DIR:-$reports/profile}"
export LYNX_TEST_MODE=1

# (Windows tools want Windows paths)
native() {
    local p
    p="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
    if command -v cygpath > /dev/null; then cygpath -w "$p"; else echo "$p"; fi
}

results=()
run() { # <label> <log> <command...>
    local label="$1" log="$2" rc
    shift 2
    echo "::group::$label"
    "$@" > "$log" 2>&1
    rc=$?
    tail -n 40 "$log"
    echo "::endgroup::"
    results+=("$([[ $rc == 0 ]] && echo PASS || echo "FAIL ($rc)")  $label")
}

clap=("$pkg"/CLAP/*.clap)
vst3=()
for b in "$pkg"/VST3/*.vst3; do [[ -e "$b" ]] && vst3+=("$b"); done
[[ -e "${clap[0]}" ]] || { echo "no CLAP in $pkg/CLAP" >&2; exit 1; }

for p in "${clap[@]}" "${vst3[@]}"; do
    name="$(basename "$p")"
    extra=()
    [[ -n "$pictures" && "$p" == *.clap ]] && extra=("$(native "$reports")/editor-")
    run "plugexporttest $name" "$reports/plugexporttest-$name.log" "$tester" "$(native "$p")" "$(native "$reports")/plugexporttest-$name.txt" "${extra[@]}"
done
for p in "${vst3[@]}"; do
    name="$(basename "$p")"
    run "pluginval $name" "$reports/pluginval-$name.log" "$pluginval" --validate "$(native "$p")" --strictness-level 10 --skip-gui-tests \
        --validate-in-process --timeout-ms 300000
done
for p in "${clap[@]}"; do
    run "clap-validator $(basename "$p")" "$reports/clap-validator.log" "$validator" validate --in-process "$(native "$p")"
done

echo
echo "LYNX plugin checks ($pkg):"
fail=0
for r in "${results[@]}"; do
    echo "  $r"
    [[ "$r" == PASS* ]] || fail=1
done
printf '%s\n' "${results[@]}" > "$reports/summary.txt"
exit $fail
