#!/usr/bin/env bash
# Points the AppImage's fixed update address (update/.htaccess on lynx-share.com) at a published release, after checking
# that release's files. It changes that one file on the site, no deploy. Run it from the release checklist.
# usage: scripts/publish-appimage-update.sh v0.1.25-alpha [--check-only]
# Anything wrong: exit 1 with the address left as it was.
set -euo pipefail
TAG="${1:?usage: publish-appimage-update.sh <tag> [--check-only]}"
CHECK_ONLY=0; [[ "${2:-}" == "--check-only" ]] && CHECK_ONLY=1
# shellcheck source=scripts/appimage-update-lib.sh
. "$(dirname "$0")/appimage-update-lib.sh"
LABEL="${TAG#v}"
APPIMAGE="LYNX-$LABEL-x86_64.AppImage"
BASE="https://github.com/$UPDATE_REPO/releases/download/$TAG"
fail() { echo "publish-appimage-update: $*" >&2; exit 1; }

[[ "$(gh api "repos/$UPDATE_REPO/releases/tags/$TAG" --jq .draft)" == "false" ]] || fail "$TAG is not a published release"

WORK="$(mktemp -d)"
trap 'rm -r -- "${WORK:?}"' EXIT
# (the public addresses, as an update check sees them, not the API's view)
curl -fsSL -o "$WORK/z" "$BASE/$APPIMAGE.zsync" || fail "no $APPIMAGE.zsync on $TAG"
curl -fsSL -o "$WORK/a" "$BASE/$APPIMAGE" || fail "no $APPIMAGE on $TAG"
header() { head -c 4096 "$1" | sed -n "s/^$2: //p" | head -1; }
[[ "$(header "$WORK/z" URL)" == "$BASE/$APPIMAGE" ]] || fail ".zsync URL is '$(header "$WORK/z" URL)', not $BASE/$APPIMAGE"
[[ "$(header "$WORK/z" Length)" == "$(stat -c%s "$WORK/a")" ]] || fail ".zsync Length does not match the AppImage"
[[ "$(header "$WORK/z" SHA-1)" == "$(sha1sum "$WORK/a" | cut -d' ' -f1)" ]] || fail ".zsync SHA-1 does not match the AppImage"
embedded="$(readelf --string-dump=.upd_info --wide "$WORK/a" | sed -n 's/^ *\[ *0\] *//p' | sed 's/ *$//')"
[[ "$embedded" == "zsync|https://lynx-share.com/update/LYNX-x86_64.AppImage.zsync" ]] || fail "the AppImage embeds '$embedded'"

# never back to an older release
CURRENT="$(update_target)" || fail "can't read $UPDATE_ADDRESS"
current_tag="$(update_target_tag "$CURRENT")"
if [[ -n "$current_tag" && "$current_tag" != "$TAG" && "$(printf '%s\n%s\n' "$current_tag" "$TAG" | sort -V | tail -1)" != "$TAG" ]]; then
    fail "the address points at $current_tag, newer than $TAG"
fi
[[ "$CHECK_ONLY" == 0 ]] || { echo "checked $TAG, address left at ${current_tag:-nothing}"; exit 0; }

update_htaccess "$BASE/$APPIMAGE.zsync" > "$WORK/htaccess"
site_upload "$WORK/htaccess" update/.htaccess || fail "the upload failed"
sleep 5
want="$(header "$WORK/z" SHA-1)"
curl -fsSL -o "$WORK/live" "$UPDATE_ADDRESS?check=$$" || fail "$UPDATE_ADDRESS does not answer after the upload"
[[ "$(header "$WORK/live" SHA-1)" == "$want" ]] || fail "$UPDATE_ADDRESS does not serve $TAG's .zsync after the upload"
echo "$UPDATE_ADDRESS -> $TAG"
