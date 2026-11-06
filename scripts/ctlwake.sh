#!/usr/bin/env bash
# Builds tools/ctlwake.cpp (round 173d2: the context that wakes each generator control) with g++
# and runs it:   scripts/ctlwake.sh --fl "<FL Studio>/Data" [--only <engine>] [--tsv out] [--known tsv] [--known-only]
# Objects go to build-ctl/. The C decoders come from build-local/ (run scripts/build-local.sh once first).
set -euo pipefail
cd "$(dirname "$0")/.."
O=build-ctl; mkdir -p "$O"
CORE=$(bash -c 'source <(grep -E "^CORE=" scripts/build-local.sh); echo $CORE')
FL="-std=c++20 -O2 -ffp-contract=off -Isrc -Ithird_party/wavpack/include -Ithird_party/vorbis/include -Ithird_party/lame/include -Ithird_party/sse2neon -Ithird_party/pocketpy -Ithird_party/monocypher -w"
NEWEST_H=$(find src \( -name "*.h" -o -name "*.inc" \) -printf "%T@ %p\n" | sort -n | tail -1 | cut -d" " -f2-)
objs=()
for f in $CORE tools/ctlwake.cpp; do objs+=("$O/$(echo "$f" | tr / _).o"); done
for f in $CORE tools/ctlwake.cpp; do
    o="$O/$(echo "$f" | tr / _).o"
    extra=""; [[ $f == src/core/Slayer.cpp ]] && extra="-fno-inline" # (GCC 15 miscompiles Slayer's x87 engine when inlining: CMakeLists.txt)
    [[ $f != tools/* && -f $o && $o -nt $f && $o -nt $NEWEST_H ]] || echo "g++ $FL $extra -c $f -o $o"
done | xargs -P "${JOBS:-8}" -I{} sh -c '{}'
[[ -d build-local/wavpack ]] || { echo "run scripts/build-local.sh first (C decoders)"; exit 1; }
g++ "${objs[@]}" build-local/wavpack/*.o build-local/vorbis/*.o build-local/lame/*.o build-local/pocketpy/*.o build-local/monocypher/*.o -o "$O/ctlwake"
"$O/ctlwake" "$@"
