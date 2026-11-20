#!/usr/bin/env bash
# Writes the licence texts of every third-party component (THIRD_PARTY.md) into one folder, for the packages:
# Apache-2.0 §4(a) (ARA SDK, Basic Pitch), MIT, BSD, LGPL and the GPLv3 option of Steinberg's SDKs want the licence text
# with the binary, not only a pointer to the source tree. Round 190nk (0.1.30).
#
#   scripts/collect-licenses.sh <folder>      write <folder> (created; e.g. the package's licenses/)
#
# Source: every LICENSE / LICENCE / COPYING / NOTICE / ThirdPartyNotices / GNU-GPL file under third_party/ (named after its
# path: third_party/ara/ARA_API/LICENSE.txt -> ara-ARA_API-LICENSE.txt), the Open Sans font licence (resources/fonts/OFL.txt)
# and resources/licenses/* (licence texts that a component names but does not carry, e.g. GPL-3.0.txt for the VST 3 and
# ASIO SDKs' GPLv3 option). LYNX SHARE's own licence (AGPL-3.0, also the licence LYNX uses JUCE under) is LICENSE next to
# the program. Plain bash + coreutils: runs in the Linux container, on macOS and in Git Bash.
set -euo pipefail
OUT="${1:?usage: scripts/collect-licenses.sh <folder>}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
cd "$(dirname "$0")/.."
n=0
while IFS= read -r f; do
    rel="${f#third_party/}"
    name="${rel//\//-}"
    case "$name" in *.txt|*.md) ;; *) name="$name.txt" ;; esac   # (opens in Notepad / TextEdit)
    cp "$f" "$OUT/$name"; n=$((n + 1))
done < <(find third_party -maxdepth 4 -type f \( -iname 'LICEN[CS]E*' -o -iname 'COPYING*' -o -iname 'NOTICE*' \
             -o -iname 'ThirdPartyNotices*' -o -iname 'GNU-GPL*' \) | LC_ALL=C sort)
cp resources/fonts/OFL.txt "$OUT/open-sans-OFL.txt"; n=$((n + 1))
for f in resources/licenses/*; do cp "$f" "$OUT/"; n=$((n + 1)); done
cat > "$OUT/README.txt" <<'TXT'
Licence texts of the third-party components in LYNX SHARE.

THIRD_PARTY.md (next to the program) lists every component, what it is used for, its licence and any change LYNX made.
Each file here is named after the component's folder in LYNX SHARE's source (https://github.com/Ka1-S/LYNX-SHARE,
folder third_party/), for example ara-ARA_API-LICENSE.txt is the ARA SDK's Apache License 2.0 and ara-NOTICE.txt its
NOTICE; onnxruntime-ThirdPartyNotices.txt is Microsoft's notice file for ONNX Runtime 1.23.2. GPL-3.0.txt is the
licence of the GPLv3 option under which LYNX uses Steinberg's VST 3 and ASIO SDKs. LYNX SHARE itself, and JUCE as LYNX
uses it, are under the GNU Affero General Public License v3 (LICENSE next to the program).
TXT
echo "collect-licenses: $n licence files in $OUT"
