#!/usr/bin/env bash
# Publishes website/ to https://lynx-share.com (Hostinger static deploy).
# Needs the Hostinger API token in ~/.config/hostinger/token (never commit it).
# The AppImage's update address (update/, see appimage-update-lib.sh) is never taken from this tree: the archive carries
# the target that is live right now, and the deploy is refused when that can't be read, and reported when it changed.
# --dry-run: everything up to the upload, then lists what the archive has under update/.
set -euo pipefail
DRY=0; [[ "${1:-}" == "--dry-run" ]] && DRY=1
SCRIPTS="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/appimage-update-lib.sh
. "$SCRIPTS/appimage-update-lib.sh"
"$SCRIPTS/site-media.sh" || { echo "deploy-site: the tutorial videos (media branch, website/_src/learn/media.json) are not in place: not deploying" >&2; exit 1; }
cd "$SCRIPTS/../website"
TARGET="$(update_target)" || { echo "deploy-site: can't read the update address $UPDATE_ADDRESS: not deploying, try again" >&2; exit 1; }
TARGET_ARM="$(update_target_aarch64)" || { echo "deploy-site: can't read the update address $UPDATE_ADDRESS_AARCH64: not deploying, try again" >&2; exit 1; }
[[ -d update ]] && echo "deploy-site: leaving website/update out of the archive (the live target stays)"
USER_=u258086853; DOMAIN=lynx-share.com
# the archive is about 450 MB: on disk (not /tmp, which can be RAM), removed however this script ends (round 190ra)
BASE="${LYNX_SITE_WORK:-$HOME/.cache/lynx-site-deploy}"; mkdir -p "$BASE"
WORK="$(mktemp -d "$BASE/XXXXXXXX")"; chmod 700 "$WORK"
trap 'rm -r -- "${WORK:?}"' EXIT
ZIP="$WORK/lynx-site.zip"
HT="$WORK/htaccess"; [[ -n "$TARGET" ]] && update_htaccess "$TARGET" "$TARGET_ARM" > "$HT" # (round 190qp: the arm64 redirect kept too)
# the archive's last file: this deploy's own line, so the live site can say that this very archive is in place
MARK="$(date -u +%FT%TZ) $(git -C "$SCRIPTS" rev-parse --short HEAD 2> /dev/null || echo -) $RANDOM$RANDOM"
ZIP="$ZIP" HT="$HT" TARGET="$TARGET" MARK="$MARK" python3 -c "
import zipfile,os
z=zipfile.ZipFile(os.environ['ZIP'],'w',zipfile.ZIP_DEFLATED)
for r,d,f in os.walk('.'):
    if r=='.' and 'update' in d: d.remove('update')
    for x in f: z.write(os.path.join(r,x),os.path.relpath(os.path.join(r,x),'.'))
if os.environ['TARGET']: z.write(os.environ['HT'],'update/.htaccess')
z.writestr('deploy.txt',os.environ['MARK']+chr(10))
z.close()"
if [[ "$DRY" == 1 ]]; then
    echo "target: ${TARGET:-none}"
    python3 -c "import zipfile,sys;z=zipfile.ZipFile(sys.argv[1]);[print('archive:',n,'|',z.read(n).decode().splitlines()[1]) for n in z.namelist() if n.startswith('update/')];print('archive files:',len(z.namelist()))" "$ZIP"
    update_reminder "$TARGET"
    exit 0
fi
T=$(cat ~/.config/hostinger/token); B=https://developers.hostinger.com; SITE="$B/api/hosting/v1/accounts/$USER_/websites/$DOMAIN"
# request <step> <curl arguments...>: the answer in $WORK/answer; an HTTP error names its step and code (round 190ra: a bare
# "curl -sf" ended the script with 22 and no word about which request failed). The error text is printed only for the API's
# own requests; the token and the upload keys never are.
request()
{
    local step="$1" code rc=0; shift
    : > "$WORK/answer"
    code="$(curl -sS -o "$WORK/answer" -w '%{http_code} %{time_total}' "$@")" || rc=$?
    echo "deploy-site: $step: HTTP ${code% *} after ${code#* } s$([[ $rc == 0 ]] || echo " (curl $rc)")" >&2
    code="${code% *}"
    [[ $rc == 0 && "$code" =~ ^2 ]] && return 0
    head -c 300 "$WORK/answer" | tr -d '\r\n' >&2; echo >&2
    return 1
}
AUTH=(-H "Authorization: Bearer $T"); JSON=(-H 'Content-Type: application/json')
request "upload address" -X POST "${AUTH[@]}" "${JSON[@]}" -d "{\"username\":\"$USER_\",\"domain\":\"$DOMAIN\"}" "$B/api/hosting/v1/files/upload-urls"
get() { python3 -c "import json,sys;print(json.load(open(sys.argv[2]))[sys.argv[1]])" "$1" "$WORK/answer"; }
URL=$(get url); AK=$(get auth_key); RK=$(get rest_auth_key); SIZE=$(stat -c%s "$ZIP")
H=(-H "X-Auth: $AK" -H "X-Auth-Rest: $RK" -H 'Tus-Resumable: 1.0.0')
request "upload start ($SIZE bytes)" -X POST "$URL/lynx-site.zip?override=true" "${H[@]}" -H "Upload-Length: $SIZE" -H 'Upload-Offset: 0'
request "upload" -X PATCH "$URL/lynx-site.zip?override=true" "${H[@]}" -H 'Upload-Offset: 0' -H 'Content-Type: application/offset+octet-stream' --data-binary @"$ZIP"
# The API answers the deploy of this large archive with "500 [Hosting:9999] Request failed" and still carries it out (seen
# 2026-11-25, 446 MB), so its answer proves nothing: what counts is the archive's deploy.txt on the live site.
request "deploy" -X POST "${AUTH[@]}" "${JSON[@]}" -d '{"archive_path":"lynx-site.zip"}' "$SITE/deploy" \
    || echo "deploy-site: the deploy request failed; checking whether the site got the archive anyway" >&2
LIVE=
for _ in {1..60}; do
    LIVE="$(curl -s --max-time 20 "https://$DOMAIN/deploy.txt?check=$RANDOM" || true)"
    [[ "$LIVE" == "$MARK" ]] && break
    sleep 5
done
[[ "$LIVE" == "$MARK" ]] || { echo "deploy-site: NOT DEPLOYED: after 5 minutes https://$DOMAIN/deploy.txt is not this archive's ('$MARK')" >&2; exit 1; }
echo "deploy-site: live: $MARK"
# the CDN keeps assets for 30 days: purge it so the new files show up at once
request "cache purge" -X DELETE "${AUTH[@]}" "$SITE/cache/clear"
for _ in 1 2 3 4 5 6; do
    sleep 10
    AFTER="$(update_target || echo unreadable)"; AFTER_ARM="$(update_target_aarch64 || echo unreadable)"
    [[ "$AFTER" == "$TARGET" && "$AFTER_ARM" == "$TARGET_ARM" ]] && break
done
if [[ "$AFTER" != "$TARGET" || "$AFTER_ARM" != "$TARGET_ARM" ]]; then
    echo "deploy-site: THE UPDATE ADDRESS CHANGED: was '${TARGET:-none}' / arm64 '${TARGET_ARM:-none}', is now '${AFTER:-none}' / arm64 '${AFTER_ARM:-none}'. Run scripts/publish-appimage-update.sh for the newest release." >&2
    exit 1
fi
update_reminder "$TARGET"
