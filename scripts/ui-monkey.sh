#!/usr/bin/env bash
# The seeded UI monkey test (tools/ui_monkey.py, round 190rb) inside a sandbox: a user + mount namespace (no root
# needed) where every folder in MONKEY_RO (default: $HOME and every drive mounted under /mnt) is read-only, so whatever the random clicks reach
# (Save as..., the file browser, Delete in a folder view) cannot change a file outside the run's own folder. The run
# folder must therefore be outside those folders (e.g. under /tmp).
#
#   scripts/ui-monkey.sh <run folder> [ui_monkey.py arguments: --app --display --seed --minutes --layout --project ...]
#
# env: MONKEY_RO="..." read-only folders; APP_LD_LIBRARY_PATH for LYNX (a devroot without system packages)
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
out="$1"; shift
mkdir -p "$out"
ro="${MONKEY_RO:-$HOME $(findmnt -rn -o TARGET | grep -E "^/mnt/[a-z]$" | tr "\n" " ")}"
for d in $ro; do
    case "$(realpath "$out")/" in "$(realpath "$d")"/*) echo "ui-monkey: the run folder must be outside $d (it is read-only in the sandbox)" >&2; exit 2 ;; esac
done
export MONKEY_RO_LIST="$ro" LD_LIBRARY_PATH="${APP_LD_LIBRARY_PATH:-${LD_LIBRARY_PATH:-}}"
exec unshare --user --map-root-user --mount bash -c '
    for d in $MONKEY_RO_LIST; do [ -d "$d" ] && mount --bind -o ro "$d" "$d"; done
    for d in $MONKEY_RO_LIST; do [ -d "$d" ] && touch "$d/.ui-monkey-rw-check" 2> /dev/null && { echo "ui-monkey: $d is still writable" >&2; exit 3; }; done
    exec python3 "$0" --out "$1" "${@:2}"' "$here/tools/ui_monkey.py" "$out" "$@"
