#!/usr/bin/env bash
# Round 190nx (issue #233 follow-up): proves that LYNX_OFFLINE=1 / --offline opens no network connection.
# Usage: tests/offline-network-test.sh /path/to/LYNX [work folder]
# Runs `LYNX --offlinetest` (the update check, going back, a crash-report upload, a live session start and join, the
# relay's HTTP) inside a new network namespace (`unshare -rn`, no network at all) under strace, once with LYNX_OFFLINE=1 and once
# with --offline, and counts the connect() calls to internet addresses (AF_INET / AF_INET6): there must be none. A control run
# without offline mode (the same route, no flag) must make some, so the strace filter is proven to see them.
# connect() to AF_UNIX (the display, the sound server) is not network access and is not counted.
set -u
LYNX="${1:?usage: offline-network-test.sh /path/to/LYNX [work folder]}"
WORK="${2:-$(mktemp -d)}"
mkdir -p "$WORK"
STRACE="${STRACE:-strace}"
command -v "$STRACE" >/dev/null || { echo "strace is needed (or set STRACE=/path/to/strace)"; exit 2; }
unshare -rn true 2>/dev/null || { echo "unshare -rn is not allowed here"; exit 2; }
fail=0
run() { # name, env assignment (or ""), extra args
    local name="$1" envs="$2"; shift 2
    local profile="$WORK/$name-profile"
    mkdir -p "$profile"
    env $envs LYNX_TEST_MODE=1 LYNX_PROFILE_DIR="$profile" XDG_CONFIG_HOME="$profile/cfg" \
        unshare -rn "$STRACE" -f -qq -e trace=connect -o "$WORK/$name.strace" \
        "$LYNX" --offlinetest "$WORK/$name.txt" "$@" >"$WORK/$name.out" 2>&1
    echo "$? $(grep -cE 'connect\(.*sa_family=AF_INET6?,' "$WORK/$name.strace" 2>/dev/null)"
}
read -r rc1 n1 <<<"$(run env LYNX_OFFLINE=1)"
read -r rc2 n2 <<<"$(run flag "" --offline)"
read -r rc3 n3 <<<"$(run control "")"
echo "control (no offline mode): internet connect() calls: $n3 (must be above 0: the filter sees them)"
[ "${n3:-0}" -gt 0 ] || { echo "FAIL: the control run made no connect() call, the check proves nothing"; fail=1; }
echo "LYNX_OFFLINE=1: exit $rc1, internet connect() calls: $n1"
echo "--offline     : exit $rc2, internet connect() calls: $n2"
[ "$rc1" = 0 ] && [ "$n1" = 0 ] || { echo "FAIL (LYNX_OFFLINE=1)"; fail=1; }
[ "$rc2" = 0 ] && [ "$n2" = 0 ] || { echo "FAIL (--offline)"; fail=1; }
grep -q "RESULT OK" "$WORK/env.txt" 2>/dev/null || { echo "FAIL: no RESULT OK in env.txt"; fail=1; }
grep -q "RESULT OK" "$WORK/flag.txt" 2>/dev/null || { echo "FAIL: no RESULT OK in flag.txt"; fail=1; }
[ $fail = 0 ] && echo "PASS: zero network connect() calls with LYNX_OFFLINE=1 and with --offline"
exit $fail
