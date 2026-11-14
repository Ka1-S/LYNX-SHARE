#!/usr/bin/env bash
# Cross build of LYNX-bridge32.exe (round 176a): the 32-bit plugin bridge, Windows x86, with Zig (clang + MinGW-w64).
# LYNX.exe (64-bit) runs every 32-bit VST2 / VST3 plugin in one of these processes (src/bridge/Bridge32Main.cpp).
#
#   scripts/build-bridge32.sh   -> $OUT/LYNX-bridge32.exe   (OUT defaults to build-win; objects in $OUT/x86)
#
# Called by scripts/build-windows-zig.sh after LYNX.exe (it reuses that script's patched JUCE copies, the version header
# and the shared object cache when it runs from there). Only the JUCE modules a plugin host needs are compiled.
set -euo pipefail
cd "$(dirname "$0")/.."

ZIG="${ZIG:-$HOME/opt/zig-x86_64-linux-0.14.1/zig}"
JUCE_DIR="${JUCE_DIR:-$HOME/opt/juce-8.0.4}"
OUT="${OUT:-build-win}"
JOBS="${JOBS:-$(nproc)}"
TARGET=x86-windows-gnu
B="$OUT/x86"
mkdir -p "$B/juce" "$B/app" "$OUT/shim"
echo '#include <dbghelp.h>' > "$OUT/shim/Dbghelp.h"

VERSION="$(sed -n 's/^project(LYNX VERSION \([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p' CMakeLists.txt)"
MODULES=(juce_core juce_data_structures juce_events juce_graphics juce_gui_basics juce_gui_extra juce_audio_basics juce_audio_processors)
{
    echo '#pragma once'
    echo '#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1'
    echo '#define JUCE_STANDALONE_APPLICATION 1'
    echo '#define JUCE_USE_CURL 0'
    echo '#define JUCE_WEB_BROWSER 0'
    echo '#define JUCE_PLUGINHOST_VST3 1'
    echo '#define JUCE_APPLICATION_NAME_STRING "LYNX-bridge32"'
    echo "#define JUCE_APPLICATION_VERSION_STRING \"$VERSION\""
    for m in "${MODULES[@]}"; do echo "#define JUCE_MODULE_AVAILABLE_$m 1"; done
} > "$B/JuceConfig.h.new"
cmp -s "$B/JuceConfig.h.new" "$B/JuceConfig.h" 2>/dev/null && rm "$B/JuceConfig.h.new" || mv "$B/JuceConfig.h.new" "$B/JuceConfig.h"

# 32-bit x86 only: JUCE hands lambdas to Win32 calls that take __stdcall callbacks (MSVC converts a lambda to any calling
# convention, clang only to the default one): those modules are compiled from copies with a static CALLBACK function
# instead (scripts/bridge32_patch_juce.py). Then the patched JUCE copies scripts/build-windows-zig.sh made (VST3 note
# ids, the dispatch hook...), else the checkout.
python3 -I scripts/bridge32_patch_juce.py "$JUCE_DIR/modules" "$OUT/juce-patched/modules" "$B/juce-x86/modules"
src_of() {
    local m="$1"
    if [[ -f "$B/juce-x86/modules/$m/$m.cpp" ]]; then echo "$B/juce-x86/modules/$m"
    elif [[ -f "$OUT/juce-patched/modules/$m/$m.cpp" ]]; then echo "$OUT/juce-patched/modules/$m"
    else echo "$JUCE_DIR/modules/$m"; fi
}

COMMON=(-target "$TARGET" -std=c++20 -O2 -DNDEBUG=1 -fno-sanitize=undefined -DLYNX_BRIDGE32=1
        -include "$B/JuceConfig.h" -include cmake/mingw/mingw_compat.h
        -I"$OUT/shim" -I"$JUCE_DIR/modules" -I"$JUCE_DIR/modules/juce_audio_processors/format_types/VST3_SDK" -Ithird_party/sse2neon -Isrc)

# the object cache of scripts/build-windows-zig.sh when it calls this script (exported there); alone: plain compiles
if ! declare -F cached > /dev/null; then
    cached() { while [[ "$1" != -- ]]; do shift; done; shift; "$@"; }
    cached_pp() { shift 2; "$@"; }
    export -f cached cached_pp
fi
JUCE_KEY="$JUCE_DIR $(cat "$B/JuceConfig.h" cmake/mingw/mingw_compat.h "$JUCE_DIR/modules/juce_core/system/juce_StandardHeader.h" | sha256sum | cut -c1-64)"
export ZIG TARGET B JUCE_KEY COMMON_STR="${COMMON[*]}"
export -f src_of
export OUT JUCE_DIR

echo "Bridge32: JUCE modules (x86, cached)..."
for m in "${MODULES[@]}" juce_core_CompilationTime juce_graphics_Harfbuzz juce_graphics_Sheenbidi; do echo "$m"; done | xargs -P "$JOBS" -I{} bash -c '
    m={}; obj="$B/juce/$m.o"
    [[ -f "$obj" && ! "$B/JuceConfig.h" -nt "$obj" ]] && exit 0
    echo "  JUCE x86 $m"
    case "$m" in
        juce_core_CompilationTime) src="$JUCE_DIR/modules/juce_core/$m.cpp"; key="$JUCE_KEY" ;;
        juce_graphics_Harfbuzz) src="$JUCE_DIR/modules/juce_graphics/$m.cpp"; key="$JUCE_KEY" ;;
        juce_graphics_Sheenbidi)
            cached "$obj" "$JUCE_DIR x86" -- "$ZIG" cc -target "$TARGET" -O2 -w -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -I"$JUCE_DIR/modules" -c "$JUCE_DIR/modules/juce_graphics/juce_graphics_Sheenbidi.c" -o "$obj"
            exit $? ;;
        *) d="$(src_of "$m")"; src="$d/$m.cpp"; key="$JUCE_KEY $(cd "$d" && find . -type f | LC_ALL=C sort | xargs -d "\n" sha256sum | sha256sum | cut -c1-64)" ;;
    esac
    cached "$obj" "$key" -- $ZIG c++ $COMMON_STR -w -c "$src" -o "$obj"'

# (rollup 0.1.28: FlWrapper.cpp reads FL .fst presets with the FLP parser since round 190fs, so the parser is linked too)
SOURCES=(src/bridge/Bridge32Main.cpp src/engine/SandboxWorker.cpp src/engine/Vst2Format.cpp src/core/FlWrapper.cpp src/flp/Flp.cpp src/platform/NativeEmbed.cpp src/platform/MingwFixes.cpp)
echo "Bridge32: sources..."
printf '%s\n' "${SOURCES[@]}" | xargs -P "$JOBS" -I{} bash -c '
    src={}; obj="$B/app/$(echo "$src" | tr / _).o"
    cached_pp "$obj" -- $ZIG c++ $COMMON_STR -Wall -Wextra -c "$src" -o "$obj"'

OBJECTS=()
for src in "${SOURCES[@]}"; do OBJECTS+=("$B/app/$(echo "$src" | tr / _).o"); done

echo "Bridge32: resources and link..."
OUT_ABS="$(cd "$OUT" && pwd)"
if [[ ! -f "$OUT/lynx_version.h" ]]; then
    IFS=. read -r VMAJ VMIN VPAT <<< "$VERSION"
    printf '#pragma once\n#define LYNX_VERSION_MAJOR %s\n#define LYNX_VERSION_MINOR %s\n#define LYNX_VERSION_PATCH %s\n#define LYNX_VERSION_STR "%s"\n' "$VMAJ" "$VMIN" "$VPAT" "$VERSION" > "$OUT/lynx_version.h"
fi
( cd resources && "$ZIG" rc /i "$OUT_ABS" /fo "$OUT_ABS/x86/LYNX-bridge32.res" LYNX-bridge32.rc )
"$ZIG" c++ -target "$TARGET" -O2 -o "$OUT/LYNX-bridge32.exe" "$B"/juce/*.o "${OBJECTS[@]}" "$B/LYNX-bridge32.res" \
    -Wl,--subsystem,windows \
    -lole32 -loleaut32 -luuid -lwinmm -lws2_32 -lshlwapi -lversion -lwininet -limm32 -lcomdlg32 \
    -lgdi32 -luser32 -lcomctl32 -lshell32 -ladvapi32 -lrpcrt4 -ldbghelp -lsetupapi -luxtheme -ldwmapi \
    -ld2d1 -ldcomp -ldwrite -ldxgi -ld3d11 -ldxguid -lwsock32 -lopengl32 -lpropsys -lmfplat -lmfuuid -lksuser
# large-address aware: 4 GB for big sample-based 32-bit instruments (zig's linker has no flag for it)
python3 -I scripts/pe_set_laa.py "$OUT/LYNX-bridge32.exe"
ls -la "$OUT/LYNX-bridge32.exe"
