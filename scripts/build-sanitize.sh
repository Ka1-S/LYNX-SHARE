#!/usr/bin/env bash
# A sanitizer build of the app, the tests and the tools (round 190rb, the 0.1.32 quality release), Linux / macOS with
# GCC or Clang:
#
#   scripts/build-sanitize.sh [extra cmake arguments...]
#
# env:  SAN=address,undefined (default; e.g. thread for TSan, which cannot be combined with address)
#       BUILD=build-san       the build folder
#       JOBS=3                parallel compiles (tests.cpp alone needs several GB under ASan)
#       NOBUILD=1             only configure (e.g. to build through your own job queue)
#
# Running (the suppression files list system libraries, third-party code and LYNX's deliberate process-life caches;
# a real LYNX leak is fixed, never suppressed). The main thread needs a big stack under ASan (its frames grow): ulimit -s 1048576
#   export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:halt_on_error=1:detect_stack_use_after_return=0:\
#          suppressions=$PWD/tests/sanitizers/asan.supp
#   export LSAN_OPTIONS=suppressions=$PWD/tests/sanitizers/lsan.supp:print_suppressions=0
#   export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0:suppressions=$PWD/tests/sanitizers/ubsan.supp
#   build-san/lynx_tests "<FL Studio>/Data"        build-san/LYNX_artefacts/RelWithDebInfo/LYNX --selftest out.txt
#
# -O1 keeps the run fast enough for the whole suite while every report still has its file:line; NDEBUG as in releases
# (the same code paths as the shipped app). LTO is off (the sanitizers' instrumentation and LTO do not mix well).
set -euo pipefail
cd "$(dirname "$0")/.."
SAN="${SAN:-address,undefined}"
BUILD="${BUILD:-build-san}"
if [[ ! -f "$BUILD/CMakeCache.txt" ]]; then
    cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1 -g -DNDEBUG" -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O1 -g -DNDEBUG" \
        -DLYNX_SANITIZE="$SAN" -DLYNX_LTO=OFF "$@"
fi
[[ "${NOBUILD:-0}" == 1 ]] && exit 0
cmake --build "$BUILD" -j "${JOBS:-3}" --target LYNX lynx_tests
