#!/usr/bin/env bash
# Re-draws every picture of the website and the README from a LYNX build, at 1x and 2x (round 190bi).
#
#   scripts/site-shots.sh <LYNX.exe | LYNX binary> [renders-folder]
#
# Every route used is off-screen (no window, no input): --showcase, --snapshot, --gridshot, --plugui, --flextrashot,
# --pluginthumbs, --danceshot, --artshot, each run once per scale with --shot-scale (the program draws the same layout
# at that pixel density: vector and text at 2x, not an upscale). Then scripts/site-shots.py converts the renders into
# website/assets/img (+ the @2x files, catalog-img.json, img-sizes.json, as website/_src/build.py reads them) and
# docs/img (README), and prints a table of what it wrote.
#
#  - A Windows exe from WSL (path ending in .exe): run through WSL's interop with Windows paths. These routes do not
#    take the foreground on builds with round 177d's fix; still, run it when nobody needs the screen.
#  - A Linux binary: needs an X display; without $DISPLAY it runs under xvfb-run, or under $LYNX_XVFB (an Xvfb binary,
#    started on display :$LYNX_XVFB_DISPLAY, default 97). Needs libasound (LD_LIBRARY_PATH is passed through).
#
# Environment: SCALES (default "1 2"), SITE (default website), DOCS (default docs/img), ROUTES (default all:
# "snapshot grid plugui extra thumbs dance art"), NO_IMPORT=1 (renders only), DRY_RUN=1 (import without writing).
# A fresh profile (LYNX_PROFILE_DIR, XDG_CONFIG_HOME) under the renders folder; LYNX_TEST_MODE=1; the theme and the
# language are the defaults (English, dark).
set -euo pipefail

usage() { sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[[ $# -ge 1 && $1 != -h && $1 != --help ]] || usage
bin=$(readlink -f "$1")
[[ -f $bin ]] || { echo "site-shots: no program at $1" >&2; exit 2; }
repo=$(cd "$(dirname "$0")/.." && pwd)
out=$(mkdir -p "${2:-$repo/build-site-shots}" && cd "${2:-$repo/build-site-shots}" && pwd)
SCALES=${SCALES:-1 2}
SITE=${SITE:-$repo/website}
DOCS=${DOCS:-$repo/docs/img}
ROUTES=${ROUTES:-snapshot grid plugui extra thumbs dance art}
want() { [[ " $ROUTES " == *" $1 "* ]]; }

windows=0
[[ $bin == *.exe ]] && windows=1
# a path as the program sees it
p() { if [[ $windows == 1 ]]; then wslpath -w "$1"; else printf '%s' "$1"; fi; }

profile="$out/profile"
mkdir -p "$profile"
unset LYNX_SHOT_SCALE LYNX_PARITYSHOT_SCALE LYNX_SHOT_SUFFIX # (the scale comes from --shot-scale below)
export LYNX_TEST_MODE=1 LYNX_PROFILE_DIR LYNX_SNAPSHOT_SIZE
LYNX_PROFILE_DIR=$(p "$profile")
export XDG_CONFIG_HOME="$profile/xdg"
if [[ $windows == 1 ]]; then
    # (WSL hands these variables to the Windows program; /p converts a path)
    export WSLENV="${WSLENV:+$WSLENV:}LYNX_TEST_MODE:LYNX_PROFILE_DIR:LYNX_SNAPSHOT_SIZE"
fi

xpid=
cleanup() { [[ -n $xpid ]] && kill "$xpid" 2> /dev/null || true; }
trap cleanup EXIT
runner=()
if [[ $windows == 0 && -z ${DISPLAY:-} ]]; then
    if [[ -n ${LYNX_XVFB:-} ]]; then
        d=${LYNX_XVFB_DISPLAY:-97}
        "$LYNX_XVFB" ":$d" -screen 0 1920x1080x24 -nolisten tcp > "$out/xvfb.log" 2>&1 &
        xpid=$!
        export DISPLAY=":$d"
        for _ in $(seq 30); do [[ -e /tmp/.X11-unix/X$d ]] && break; sleep 0.1; done
        sleep 1
        kill -0 "$xpid" 2> /dev/null || { echo "site-shots: $LYNX_XVFB did not start (see $out/xvfb.log)" >&2; exit 2; }
    elif command -v xvfb-run > /dev/null; then
        runner=(xvfb-run -a -s "-screen 0 1920x1080x24")
    else
        echo "site-shots: no display; install xvfb (xvfb-run) or set LYNX_XVFB=/path/to/Xvfb" >&2
        exit 2
    fi
fi

log="$out/site-shots.log"
: > "$log"
lynx() { # lynx <name> <args...>: one run, its output in the log, its time on stdout
    local name=$1 t0=$SECONDS rc=0
    shift
    printf '%s\n' "== $name: $(basename "$bin") $*" >> "$log"
    "${runner[@]}" "$bin" "$@" >> "$log" 2>&1 || rc=$?
    printf '  %-22s %4ss  exit %s\n' "$name" $((SECONDS - t0)) "$rc"
    return 0
}

echo "site-shots: $bin -> $out (scales: $SCALES)"
demo="$out/Demo.lynx"
[[ -f $demo ]] || lynx showcase --showcase "$(p "$demo")"
[[ -f $demo ]] || { echo "site-shots: --showcase wrote no demo project (see $log)" >&2; exit 1; }

for s in $SCALES; do
    x="$out/x$s"
    want thumbs && rm -rf -- "${x:?}/thumbs" # (--pluginthumbs skips the pictures that exist: start clean)
    mkdir -p "$x"/{snap,snap1600,grid,grid1600,plugui,extra,thumbs,dance}
    echo " scale $s -> $x"
    if want snapshot; then
        LYNX_SNAPSHOT_SIZE=1920x1080 lynx "snapshot 1920 @$s" --snapshot "$(p "$x/snap/s")" "$(p "$demo")" --shot-scale "$s"
        LYNX_SNAPSHOT_SIZE=1600x900 lynx "snapshot 1600 @$s" --snapshot "$(p "$x/snap1600/s")" "$(p "$demo")" --shot-scale "$s"
    fi
    if want grid; then
        LYNX_SNAPSHOT_SIZE=1920x1080 lynx "gridshot 1920 @$s" --gridshot "$(p "$x/grid/g")" --shot-scale "$s"
        LYNX_SNAPSHOT_SIZE=1600x900 lynx "gridshot 1600 @$s" --gridshot "$(p "$x/grid1600/g")" --shot-scale "$s"
    fi
    want plugui && lynx "plugui @$s" --plugui "$(p "$x/plugui")" --shot-scale "$s"
    want extra && lynx "flextrashot @$s" --flextrashot "$(p "$x/extra")" --shot-scale "$s"
    # (with a scale given, --pluginthumbs draws each plugin's whole editor, not the picker's 258 x 128 thumbnail)
    # (it skips the pictures that exist, so a run that stops early is simply run again while it still adds pictures:
    # the Linux build of 0.1.26-dev can end with bad_function_call after Drumpad in one long run, round 190bi)
    if want thumbs; then
        n=-1
        for try in 1 2 3 4 5; do
            have=$(find "$x/thumbs" -name '*.png' | wc -l)
            [[ $have == "$n" ]] && break
            n=$have
            lynx "pluginthumbs @$s #$try" --pluginthumbs "$(p "$x/thumbs")" --shot-scale "$s"
            grep -q '^drawn ' "$log" && [[ $(tail -n 3 "$log" | grep -c '^drawn ') -gt 0 ]] && break
        done
    fi
    want dance && lynx "danceshot @$s" --danceshot "$(p "$x/dance/d")" --shot-scale "$s"
done
# --artshot draws 100 % and 200 % itself (art/<name>-after-100.png and -200.png)
want art && { mkdir -p "$out/art"; lynx "artshot 100+200" --artshot "$(p "$out/art")" after; }

[[ ${NO_IMPORT:-0} == 1 ]] && { echo "site-shots: renders in $out (NO_IMPORT=1)"; exit 0; }
python3 "$repo/scripts/site-shots.py" "$out" --site "$SITE" --docs "$DOCS" ${DRY_RUN:+--dry-run}
