#!/usr/bin/env bash
# Compiler wrapper used by scripts/build-local.sh for g++ / clang++ (not zig, which caches objects itself).
#
# `$CXX a.cpp b.cpp ... -o x` compiles its sources one after the other, and every tool build-local.sh links against the
# core compiled the whole core again: 9 serial core compiles were ~90 of the ~100 minutes of a CI "Core" job (round
# speed185a). Through this wrapper each source is compiled ONCE per build (keyed on the source path and the flags),
# LYNX_BUILD_JOBS compiles at a time, and every link uses the objects. Same compiler, same flags: the same code.
#
#   LYNX_CXX_ONCE_REAL   the real compiler command (words split on spaces, so "ccache g++" works)
#   LYNX_CXX_ONCE_RUN    an id of the current build: objects are reused within it, compiled again in the next one
#   LYNX_BUILD_JOBS      parallel compiles (default: the CPUs)
# A command with -c (one translation unit) is passed straight to the compiler.
set -u
read -r -a real <<< "${LYNX_CXX_ONCE_REAL:?LYNX_CXX_ONCE_REAL is not set}"
run="${LYNX_CXX_ONCE_RUN:?LYNX_CXX_ONCE_RUN is not set}"

if [[ "${1:-}" == --one ]]; then # --one <flags file> <object> <source>: one compile (through xargs below)
    flagsfile="$2" o="$3" src="$4" flags=()
    while IFS= read -r -d '' f; do flags+=("$f"); done < "$flagsfile"
    "${real[@]}" "${flags[@]}" -c "$src" -o "$o.tmp" && mv -f "$o.tmp" "$o"
    exit
fi

for a in "$@"; do [[ "$a" == -c ]] && exec "${real[@]}" "$@"; done

# compile flags = every argument but the sources, the objects / libraries and the output
flags=() others=() srcs=() out=
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o) out="$2"; shift ;;
        -iquote | -isystem | -idirafter | -include | -imacros | -I | -D | -U | -x | -target | -arch) flags+=("$1" "$2"); shift ;;
        *.cpp | *.cc | *.cxx) srcs+=("$1") ;;
        -*) flags+=("$1") ;;
        *) others+=("$1") ;;
    esac
    shift
done
[[ ${#srcs[@]} -gt 0 && -n "$out" ]] || exec "${real[@]}" "${flags[@]}" ${others[@]+"${others[@]}"} ${out:+-o "$out"}

key="$(printf '%s\n' "${real[*]}" "${flags[@]}" | cksum | cut -d' ' -f1)"
dir="build-local/obj/$key"
mkdir -p "$dir"
[[ -f "$dir/flags" ]] || { printf '%s\0' "${flags[@]}" > "$dir/flags.tmp$$" && mv -f "$dir/flags.tmp$$" "$dir/flags"; }
stamp="$dir/.done-$run"
touch "$stamp"
objs=() todo=()
for s in "${srcs[@]}"; do
    o="$dir/$(printf '%s' "$s" | tr '/.' '__').o"
    objs+=("$o")
    if ! grep -qxF "$s" "$stamp"; then
        todo+=("$o" "$s")
        echo "$s" >> "$stamp"
    fi
done
if [[ ${#todo[@]} -gt 0 ]]; then
    jobs="${LYNX_BUILD_JOBS:-$(nproc 2> /dev/null || sysctl -n hw.ncpu 2> /dev/null || echo 4)}"
    if ! printf '%s\n' "${todo[@]}" | xargs -n 2 -P "$jobs" "$0" --one "$dir/flags"; then
        rm -f "$stamp" # (nothing of this run counts as compiled any more: the build stops here anyway)
        exit 1
    fi
fi
exec "${real[@]}" "${flags[@]}" "${objs[@]}" ${others[@]+"${others[@]}"} -o "$out"
