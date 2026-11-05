#!/usr/bin/env bash
# Builds tools/fxfuzz.cpp with AddressSanitizer + UBSan (g++) and runs it (round 111):
#   scripts/fxfuzz-asan.sh [fxfuzz options...]     e.g. --rounds 4, --dir "<FL Studio>/Data", --only <kind>, --verbose
# Objects go to build-asan/ (rebuilt when a source or any header changed). The C decoders come from build-local/.
set -euo pipefail
cd "$(dirname "$0")/.."
O=build-asan; mkdir -p "$O"
CORE=$(bash -c 'source <(grep -E "^CORE=" scripts/build-local.sh); echo $CORE')
FL="-std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Isrc -Ithird_party/wavpack/include -Ithird_party/vorbis/include -Ithird_party/lame/include -Ithird_party/sse2neon -Ithird_party/pocketpy -Ithird_party/monocypher -w"
NEWEST_H=$(ls -t $(find src -name "*.h" -o -name "*.inc") | head -1)
objs=()
for f in $CORE tools/fxfuzz.cpp; do objs+=("$O/$(echo "$f" | tr / _).o"); done
for f in $CORE tools/fxfuzz.cpp; do
    o="$O/$(echo "$f" | tr / _).o"
    [[ -f $o && $o -nt $f && $o -nt $NEWEST_H ]] || echo "g++ $FL -c $f -o $o"
done | xargs -P "${JOBS:-8}" -I{} sh -c '{}'
[[ -d build-local/wavpack ]] || { echo "run scripts/build-local.sh first (C decoders)"; exit 1; }
g++ -fsanitize=address,undefined "${objs[@]}" build-local/wavpack/*.o build-local/vorbis/*.o build-local/lame/*.o build-local/pocketpy/*.o build-local/monocypher/*.o -o "$O/fxfuzz"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} "$O/fxfuzz" "$@"
