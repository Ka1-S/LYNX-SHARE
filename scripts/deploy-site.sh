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
cd "$SCRIPTS/../website"
TARGET="$(update_target)" || { echo "deploy-site: can't read the update address $UPDATE_ADDRESS: not deploying, try again" >&2; exit 1; }
[[ -d update ]] && echo "deploy-site: leaving website/update out of the archive (the live target stays)"
USER_=u258086853; DOMAIN=lynx-share.com
ZIP=$(mktemp -d)/lynx-site.zip
HT="$(mktemp)"; [[ -n "$TARGET" ]] && update_htaccess "$TARGET" > "$HT"
ZIP="$ZIP" HT="$HT" TARGET="$TARGET" python3 -c "
import zipfile,os
z=zipfile.ZipFile(os.environ['ZIP'],'w',zipfile.ZIP_DEFLATED)
for r,d,f in os.walk('.'):
    if r=='.' and 'update' in d: d.remove('update')
    for x in f: z.write(os.path.join(r,x),os.path.relpath(os.path.join(r,x),'.'))
if os.environ['TARGET']: z.write(os.environ['HT'],'update/.htaccess')
z.close()"
if [[ "$DRY" == 1 ]]; then
    echo "target: ${TARGET:-none}"
    python3 -c "import zipfile,sys;z=zipfile.ZipFile(sys.argv[1]);[print('archive:',n,'|',z.read(n).decode().splitlines()[1]) for n in z.namelist() if n.startswith('update/')];print('archive files:',len(z.namelist()))" "$ZIP"
    update_reminder "$TARGET"
    exit 0
fi
T=$(cat ~/.config/hostinger/token); B=https://developers.hostinger.com
J=$(curl -sf -X POST -H "Authorization: Bearer $T" -H 'Content-Type: application/json' -d "{\"username\":\"$USER_\",\"domain\":\"$DOMAIN\"}" $B/api/hosting/v1/files/upload-urls)
get() { python3 -c "import json,sys;print(json.load(sys.stdin)['$1'])" <<<"$J"; }
URL=$(get url); AK=$(get auth_key); RK=$(get rest_auth_key); SIZE=$(stat -c%s "$ZIP")
H=(-H "X-Auth: $AK" -H "X-Auth-Rest: $RK" -H 'Tus-Resumable: 1.0.0')
curl -sf -o /dev/null -X POST "$URL/lynx-site.zip?override=true" "${H[@]}" -H "Upload-Length: $SIZE" -H 'Upload-Offset: 0'
curl -sf -o /dev/null -X PATCH "$URL/lynx-site.zip?override=true" "${H[@]}" -H 'Upload-Offset: 0' -H 'Content-Type: application/offset+octet-stream' --data-binary @"$ZIP"
curl -sf -X POST -H "Authorization: Bearer $T" -H 'Content-Type: application/json' -d '{"archive_path":"lynx-site.zip"}' $B/api/hosting/v1/accounts/$USER_/websites/$DOMAIN/deploy; echo
# the CDN keeps assets for 30 days: purge it so the new files show up at once
curl -sf -X DELETE -H "Authorization: Bearer $T" "$B/api/hosting/v1/accounts/$USER_/websites/$DOMAIN/cache/clear"; echo
# (the deploy is accepted first and runs after: give it a moment)
for _ in 1 2 3 4 5 6; do
    sleep 10
    AFTER="$(update_target || echo unreadable)"
    [[ "$AFTER" == "$TARGET" ]] && break
done
if [[ "$AFTER" != "$TARGET" ]]; then
    echo "deploy-site: THE UPDATE ADDRESS CHANGED: was '${TARGET:-none}', is now '${AFTER:-none}'. Run scripts/publish-appimage-update.sh for the newest release." >&2
    exit 1
fi
update_reminder "$TARGET"
