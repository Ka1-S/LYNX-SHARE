#!/usr/bin/env bash
# Builds tools/genfuzz.cpp (the generators' fuzzer, round 170i2) with AddressSanitizer + UBSan (g++) and runs it:
#   scripts/genfuzz-asan.sh [genfuzz options...]     e.g. --rounds 2, --fl "<FL Studio>/Data", --dir <folder>, --only <name>
# Objects go to build-asan/ (shared with scripts/fxfuzz-asan.sh). The C decoders come from build-local/.
set -euo pipefail
cd "$(dirname "$0")/.."
O=build-asan; mkdir -p "$O"
CORE=$(bash -c 'source <(grep -E "^CORE=" scripts/build-local.sh); echo $CORE')
FL="-std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Isrc -Ithird_party/wavpack/include -Ithird_party/vorbis/include -Ithird_party/lame/include -Ithird_party/sse2neon -Ithird_party/pocketpy -Ithird_party/monocypher -w"
NEWEST_H=$(ls -t $(find src -name "*.h" -o -name "*.inc") | head -1)
objs=()
for f in $CORE tools/genfuzz.cpp; do objs+=("$O/$(echo "$f" | tr / _).o"); done
for f in $CORE tools/genfuzz.cpp; do
    o="$O/$(echo "$f" | tr / _).o"
    extra=""; [[ $f == src/core/Slayer.cpp ]] && extra="-fno-inline" # (GCC 15 miscompiles Slayer's x87 engine when inlining: CMakeLists.txt)
    [[ -f $o && $o -nt $f && $o -nt $NEWEST_H ]] || echo "g++ $FL $extra -c $f -o $o"
done | xargs -P "${JOBS:-8}" -I{} sh -c '{}'
[[ -d build-local/wavpack ]] || { echo "run scripts/build-local.sh first (C decoders)"; exit 1; }
g++ -fsanitize=address,undefined "${objs[@]}" build-local/wavpack/*.o build-local/vorbis/*.o build-local/lame/*.o build-local/pocketpy/*.o build-local/monocypher/*.o -o "$O/genfuzz"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} "$O/genfuzz" "$@"
