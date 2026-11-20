#!/usr/bin/env bash
# Puts the site's big media into website/ before a deploy: the tutorial videos (MP4) are kept on the repository's `media`
# branch under site/learn/, not in main, so cloning stays small. website/_src/learn/media.json lists each file with its
# size and SHA-256; the files land in website/assets/media/learn/ (ignored by git) and are checked against that list.
#   scripts/site-media.sh            fetch origin/media and extract the files (deploy-site.sh runs this)
#   scripts/site-media.sh --check    only check the files already in place
# Adding a video: put it in site/learn/ on the `media` branch, add its entry to media.json (size + sha256), build.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/website/assets/media/learn"
if [[ "${1:-}" != "--check" ]]; then
    git -C "$ROOT" fetch -q origin media
    mkdir -p "$DEST"
    git -C "$ROOT" archive origin/media site/learn | tar -x -C "$DEST" --strip-components=2
fi
python3 - "$ROOT" <<'PY'
import hashlib, json, sys
from pathlib import Path
root = Path(sys.argv[1]); site = root / "website"
media = json.loads((site / "_src/learn/media.json").read_text(encoding="utf-8"))
bad = []
for path, want in media.items():
    f = site / path
    if not f.is_file():
        bad.append(f"missing {path}")
    elif f.stat().st_size != want["size"] or hashlib.sha256(f.read_bytes()).hexdigest() != want["sha256"]:
        bad.append(f"differs from media.json: {path}")
for b in bad:
    print("site-media:", b)
print(f"site-media: {len(media)} files, {len(bad)} problems")
sys.exit(1 if bad else 0)
PY
