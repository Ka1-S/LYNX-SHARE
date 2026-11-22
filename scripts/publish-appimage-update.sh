#!/usr/bin/env bash
# Points the AppImage's fixed update address (update/.htaccess on lynx-share.com) at a published release, after checking
# that release's files. It changes that one file on the site, no deploy. Run it from the release checklist.
# usage: scripts/publish-appimage-update.sh v0.1.25-alpha [--check-only]
# A release with an arm64 AppImage (LYNX-<label>-aarch64.AppImage, from 0.1.31) also gets its own address,
# update/LYNX-aarch64.AppImage.zsync, checked the same way (round 190oy).
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
header() { head -c 4096 "$1" | sed -n "s/^$2: //p" | head -1; }
# check_appimage <AppImage name> <work prefix> <fixed address>: the release's .zsync and AppImage belong together and
# the AppImage asks the fixed address for its updates
check_appimage() {
    local name="$1" w="$2" address="$3" embedded
    curl -fsSL -o "$w.z" "$BASE/$name.zsync" || fail "no $name.zsync on $TAG"
    curl -fsSL -o "$w.a" "$BASE/$name" || fail "no $name on $TAG"
    [[ "$(header "$w.z" URL)" == "$BASE/$name" ]] || fail "$name.zsync URL is '$(header "$w.z" URL)', not $BASE/$name"
    [[ "$(header "$w.z" Length)" == "$(stat -c%s "$w.a")" ]] || fail "$name.zsync Length does not match the AppImage"
    [[ "$(header "$w.z" SHA-1)" == "$(sha1sum "$w.a" | cut -d' ' -f1)" ]] || fail "$name.zsync SHA-1 does not match the AppImage"
    # (readelf reads the .upd_info section of either architecture's AppImage)
    embedded="$(readelf --string-dump=.upd_info --wide "$w.a" | sed -n 's/^ *\[ *0\] *//p' | sed 's/ *$//')"
    [[ "$embedded" == "zsync|$address" ]] || fail "$name embeds '$embedded'"
}
check_appimage "$APPIMAGE" "$WORK/x86" "$UPDATE_ADDRESS"
# the arm64 AppImage, when the release has one
ARM_APPIMAGE="LYNX-$LABEL-aarch64.AppImage" ARM_ZSYNC=
if gh api "repos/$UPDATE_REPO/releases/tags/$TAG" --jq '.assets[].name' | grep -qxF "$ARM_APPIMAGE.zsync"; then
    check_appimage "$ARM_APPIMAGE" "$WORK/arm" "$UPDATE_ADDRESS_AARCH64"
    ARM_ZSYNC="$BASE/$ARM_APPIMAGE.zsync"
fi

# never back to an older release
CURRENT="$(update_target)" || fail "can't read $UPDATE_ADDRESS"
current_tag="$(update_target_tag "$CURRENT")"
if [[ -n "$current_tag" && "$current_tag" != "$TAG" && "$(printf '%s\n%s\n' "$current_tag" "$TAG" | sort -V | tail -1)" != "$TAG" ]]; then
    fail "the address points at $current_tag, newer than $TAG"
fi
[[ "$CHECK_ONLY" == 0 ]] || { echo "checked $TAG${ARM_ZSYNC:+ (and its arm64 AppImage)}, address left at ${current_tag:-nothing}"; exit 0; }

update_htaccess "$BASE/$APPIMAGE.zsync" "$ARM_ZSYNC" > "$WORK/htaccess"
site_upload "$WORK/htaccess" update/.htaccess || fail "the upload failed"
sleep 5
want="$(header "$WORK/x86.z" SHA-1)"
curl -fsSL -o "$WORK/live" "$UPDATE_ADDRESS?check=$$" || fail "$UPDATE_ADDRESS does not answer after the upload"
[[ "$(header "$WORK/live" SHA-1)" == "$want" ]] || fail "$UPDATE_ADDRESS does not serve $TAG's .zsync after the upload"
echo "$UPDATE_ADDRESS -> $TAG"
if [[ -n "$ARM_ZSYNC" ]]; then
    curl -fsSL -o "$WORK/live-arm" "$UPDATE_ADDRESS_AARCH64?check=$$" || fail "$UPDATE_ADDRESS_AARCH64 does not answer after the upload"
    [[ "$(header "$WORK/live-arm" SHA-1)" == "$(header "$WORK/arm.z" SHA-1)" ]] || fail "$UPDATE_ADDRESS_AARCH64 does not serve $TAG's .zsync after the upload"
    echo "$UPDATE_ADDRESS_AARCH64 -> $TAG"
fi
