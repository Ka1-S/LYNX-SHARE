#!/usr/bin/env bash
# LYNX's plugins for other hosts as the optional download of a release (round 188b, docs/PLUGIN_EXPORT.md): one archive per
# OS, never inside the app's installer. The names must not end like the app's files (the updater picks those by suffix,
# core/UpdateCheck.cpp; tests/PluginExport180bTests.inc checks it):
#   LYNX-Plugins-<label>-win-x64.zip           (windows)
#   LYNX-Plugins-<label>-linux-x86_64.tar.xz   (linux)
#   LYNX-Plugins-<label>-macos-universal.tar.xz   (macos)
# Inside: LYNX-Plugins-<label>/{CLAP/LYNX Plugins.clap, VST3/*.vst3, PLUGINS.txt, README.txt, install script, LICENSE,
# THIRD_PARTY.md}. The plugins come from the build's plugins/ folder (cmake/LynxPlugins.cmake, scripts/build-windows-zig.sh).
#
#   scripts/package-plugins.sh <windows|linux|macos> <plugins dir> <label> <dist dir>
#   scripts/package-plugins.sh --sums <label> <dist dir>      LYNX-Plugins-<label>-SHA256SUMS.txt over the archives there
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ "${1:-}" == --sums ]]; then
    label="$2" dist="$3"
    cd "$dist"
    shopt -s nullglob
    files=(LYNX-Plugins-"$label"-*.zip LYNX-Plugins-"$label"-*.tar.xz)
    (( ${#files[@]} > 0 )) || { echo "no plugin archives for $label in $dist" >&2; exit 1; }
    sha256sum -- "${files[@]}" > "LYNX-Plugins-$label-SHA256SUMS.txt"
    cat "LYNX-Plugins-$label-SHA256SUMS.txt"
    exit 0
fi

os="$1" plugins="$2" label="$3" dist="$4"
case "$os" in
    windows) archive="LYNX-Plugins-$label-win-x64.zip" osname="Windows x64" ;;
    linux) archive="LYNX-Plugins-$label-linux-x86_64.tar.xz" osname="Linux x86_64" ;;
    macos) archive="LYNX-Plugins-$label-macos-universal.tar.xz" osname="macOS, Apple silicon and Intel" ;;
    *) echo "unknown OS: $os" >&2; exit 2 ;;
esac
[[ -e "$plugins/LYNX Plugins.clap" ]] || { echo "no LYNX Plugins.clap in $plugins" >&2; exit 1; }

mkdir -p "$dist"
dist="$(cd "$dist" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "${work:?}"' EXIT
top="LYNX-Plugins-$label"
pkg="$work/$top"
mkdir -p "$pkg/CLAP" "$pkg/VST3"

cp -R "$plugins/LYNX Plugins.clap" "$pkg/CLAP/"
# the table says which entries ship, and which of them as VST3 (built = 2); every one of those must be there
{
    echo "LYNX plugins $label ($osname)"
    echo
    printf '%-26s %-11s %s\n' "Plugin" "Kind" "Formats"
} > "$pkg/PLUGINS.txt"
missing=0
while IFS='~' read -r name instrument built; do
    kind=effect; [[ "$instrument" == 1 ]] && kind=instrument
    formats=CLAP
    if [[ "$built" == 2 ]]; then
        formats="CLAP, VST3"
        if [[ -e "$plugins/$name.vst3" ]]; then cp -R "$plugins/$name.vst3" "$pkg/VST3/"; else echo "missing: $plugins/$name.vst3" >&2; missing=1; fi
    fi
    printf '%-26s %-11s %s\n' "$name" "$kind" "$formats" >> "$pkg/PLUGINS.txt"
done < <(sed -nE 's/^LYNX_PLUGIN \("([^"]+)", ([01]), [^,]+, [^,]+, "[^"]+", 0x[0-9a-f]+, "[^"]+", ([12])\)$/\1~\2~\3/p' src/plugin/plugins.def)
[[ $missing == 0 ]] || exit 1

sed -e "s/@LABEL@/$label/g" -e "s/@OS@/$osname/g" resources/plugin/README.txt > "$pkg/README.txt"
cp LICENSE THIRD_PARTY.md "$pkg/"
if [[ "$os" == windows ]]; then
    for f in README.txt PLUGINS.txt; do sed -i 's/$/\r/' "$pkg/$f"; done # (Notepad)
    sed 's/$/\r/' resources/plugin/install.cmd > "$pkg/install.cmd"
else
    install -m 755 resources/plugin/install.sh "$pkg/install.sh"
fi

# smaller, and no build paths inside: Linux strips the symbols; macOS strips the local ones and then signs each bundle ad
# hoc (an arm64 binary must carry a signature; there is no Apple Developer ID yet), the signature last
if [[ "$os" == linux ]]; then
    find "$pkg" -type f \( -name '*.so' -o -name '*.clap' \) -exec strip --strip-unneeded {} +
elif [[ "$os" == macos ]]; then
    find "$pkg" -type f -path '*/Contents/MacOS/*' -exec strip -x {} +
    for b in "$pkg/CLAP/"*.clap "$pkg/VST3/"*.vst3; do
        [[ -e "$b" ]] && codesign --force --sign - --timestamp=none "$b"
    done
fi

rm -f "${dist:?}/$archive"
case "$os" in
    windows) ( cd "$work" && zip -q -r -9 -X "$dist/$archive" "$top" ) ;;
    # (one solid stream with a dictionary larger than a plugin: the VST3s share most of their bytes with the CLAP; macOS'
    # Archive Utility opens it too, and the bundles' signatures are files inside them)
    linux|macos) tar -C "$work" -cf - "$top" | xz -T1 --lzma2=preset=9e,dict=256MiB > "$dist/$archive" ;;
esac
ls -la "$dist/$archive"
