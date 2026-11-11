#!/usr/bin/env bash
# Starts a LYNX AppImage the way the AppImage catalog test does: a fresh home folder, no network (when a network
# namespace can be created), on a virtual X display; passes when LYNX is still running after WAIT seconds and shows
# a top-level window. Needs Xvfb and xwininfo (packages xvfb, x11-utils).
#
# FONTS=none: the same on a system where no font is found at all, neither by JUCE's own folder scan nor by fontconfig
# (issue #142: on NixOS JUCE found no monospaced font, and 0.1.25 crashed while drawing its first text with one).
#
# usage: [FONTS=none] scripts/check-appimage-start.sh <LYNX AppImage> [seconds to wait, default 12]
set -uo pipefail
AI="$(readlink -f "$1")"; WAIT="${2:-12}"
WORK="$(mktemp -d)"
DISP=:57
Xvfb "$DISP" -screen 0 1600x1000x24 -nolisten tcp > "$WORK/xvfb.log" 2>&1 &
XPID=$!
trap 'kill "$APID" "$XPID" 2> /dev/null; sleep 1; kill -9 "$APID" 2> /dev/null; rm -rf "$WORK"' EXIT
APID=
for _ in $(seq 50); do [[ -e /tmp/.X11-unix/X${DISP#:} ]] && break; sleep 0.1; done

# REQUIRE_OFFLINE=1 (CI) fails when no network namespace can be made
offline=()
if unshare -n true 2> /dev/null; then offline=(unshare -n); echo "network: none (unshare -n)"
elif unshare -rn true 2> /dev/null; then offline=(unshare -rn); echo "network: none (unshare -rn)"
elif [[ "${REQUIRE_OFFLINE:-0}" == 1 ]]; then echo "ERROR: cannot create a network namespace"; exit 1
else echo "network: left on (no network namespace in this environment)"; fi

fonts=()
if [[ "${FONTS:-}" == none ]]; then
    mkdir -p "$WORK/nofonts"
    printf '<?xml version="1.0"?>\n<fontconfig><dir>%s</dir></fontconfig>\n' "$WORK/nofonts" > "$WORK/fonts.conf"
    fonts=(env "JUCE_FONT_PATH=$WORK/nofonts" "FONTCONFIG_FILE=$WORK/fonts.conf")
    echo "fonts: none (JUCE_FONT_PATH and FONTCONFIG_FILE name an empty folder)"
fi

mkdir -p "$WORK/home"
HOME="$WORK/home" XDG_CONFIG_HOME="$WORK/home/.config" XDG_CACHE_HOME="$WORK/home/.cache" DISPLAY="$DISP" \
    APPIMAGE_EXTRACT_AND_RUN=1 "${offline[@]}" "${fonts[@]}" "$AI" > "$WORK/run.log" 2>&1 &
APID=$!
sleep "$WAIT"
echo "--- output"; head -50 "$WORK/run.log"
if ! kill -0 "$APID" 2> /dev/null; then
    wait "$APID"; echo "ERROR: LYNX exited within $WAIT s (exit $?)"; exit 1
fi
wins="$(DISPLAY="$DISP" xwininfo -root -children 2> /dev/null | grep -E '^ +0x[0-9a-f]+ "[^"]+"' || true)"
echo "--- top-level windows"; echo "$wins"
grep -q '"LYNX' <<< "$wins" || { echo "ERROR: no LYNX window after $WAIT s"; exit 1; }
echo "OK: LYNX is running after $WAIT s with its window"
