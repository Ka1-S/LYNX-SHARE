#!/bin/sh
# Installs LYNX's plugins for other hosts for the current user (no root), on Linux or macOS:
#   Linux  CLAP -> ~/.clap   VST3 -> ~/.vst3
#   macOS  CLAP -> ~/Library/Audio/Plug-Ins/CLAP   VST3 -> ~/Library/Audio/Plug-Ins/VST3
# Run it again to update; ./install.sh --uninstall removes exactly what it installed (the names in PLUGINS.txt).
set -e
here="$(cd "$(dirname "$0")" && pwd)"
if [ "$(uname -s)" = Darwin ]; then
    clap="$HOME/Library/Audio/Plug-Ins/CLAP"
    vst3="$HOME/Library/Audio/Plug-Ins/VST3"
else
    clap="$HOME/.clap"
    vst3="$HOME/.vst3"
fi

if [ "$1" = "--uninstall" ]; then
    rm -rf "$clap/LYNX Plugins.clap"
    for b in "$here"/VST3/*.vst3; do
        [ -e "$b" ] && rm -rf "$vst3/$(basename "$b")"
    done
    echo "LYNX plugins removed from $clap and $vst3."
    exit 0
fi

mkdir -p "$clap" "$vst3"
rm -rf "$clap/LYNX Plugins.clap"
cp -R "$here/CLAP/LYNX Plugins.clap" "$clap/"
for b in "$here"/VST3/*.vst3; do
    [ -e "$b" ] || continue
    rm -rf "$vst3/$(basename "$b")"
    cp -R "$b" "$vst3/"
done
if [ "$(uname -s)" = Darwin ]; then
    # a download is quarantined; these bundles are signed ad hoc only (no Apple Developer ID yet)
    xattr -dr com.apple.quarantine "$clap/LYNX Plugins.clap" 2> /dev/null || true
    for b in "$here"/VST3/*.vst3; do
        [ -e "$b" ] && xattr -dr com.apple.quarantine "$vst3/$(basename "$b")" 2> /dev/null || true
    done
fi
echo "LYNX plugins installed: CLAP in $clap, VST3 in $vst3. Rescan the plugins in your host."
