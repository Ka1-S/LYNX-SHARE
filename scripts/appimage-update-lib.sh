# Shared by scripts/deploy-site.sh and scripts/publish-appimage-update.sh: the AppImage's fixed update address.
# The live site holds the address's target (update/.htaccess, a 302 to a release's .zsync), not this repository, so a site
# deploy from an old checkout cannot move or remove it.
UPDATE_ADDRESS="${LYNX_UPDATE_ADDRESS:-https://lynx-share.com/update/LYNX-x86_64.AppImage.zsync}"
UPDATE_REPO="${LYNX_UPDATE_REPO:-Ka1-S/LYNX-SHARE}"
# the first release whose AppImage reads the address
UPDATE_FIRST_VERSION="v0.1.25"

# Prints where the address points now, nothing when it has no target yet. Fails when the site can't tell (no answer, an
# error, a target that isn't a release's .zsync): callers must not go on guessing.
update_target()
{
    local answer code url
    answer="$(curl -s -o /dev/null -w '%{http_code} %{redirect_url}' --max-time 30 "$UPDATE_ADDRESS?check=$$")" || return 1
    code="${answer%% *}"; url="${answer#* }"
    case "$code" in
        404) return 0 ;;
        302) [[ "$url" =~ ^https://github\.com/$UPDATE_REPO/releases/download/v[^/]+/LYNX-[^/]+\.AppImage\.zsync$ ]] || return 1
             printf '%s\n' "$url" ;;
        *) return 1 ;;
    esac
}

# The tag in a target URL.
update_target_tag() { sed -n 's#.*/releases/download/\([^/]*\)/.*#\1#p' <<<"$1"; }

# update/.htaccess for a target URL, on stdout.
update_htaccess()
{
    printf '%s\n' "# Written by scripts/publish-appimage-update.sh. The AppImage's fixed update address: the newest published release." \
                  "Redirect 302 /update/LYNX-x86_64.AppImage.zsync $1" \
                  '<IfModule mod_headers.c>' 'Header always set Cache-Control "no-store"' '</IfModule>'
}

# Uploads one file to a path of the site, nothing else: site_upload <local file> <path on the site>
site_upload()
{
    local token base=https://developers.hostinger.com answer url auth rest size
    token="$(cat ~/.config/hostinger/token)"
    answer="$(curl -sf -X POST -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
              -d '{"username":"u258086853","domain":"lynx-share.com"}' "$base/api/hosting/v1/files/upload-urls")" || return 1
    url="$(python3 -c "import json,sys;print(json.load(sys.stdin)['url'])" <<<"$answer")"
    auth="$(python3 -c "import json,sys;print(json.load(sys.stdin)['auth_key'])" <<<"$answer")"
    rest="$(python3 -c "import json,sys;print(json.load(sys.stdin)['rest_auth_key'])" <<<"$answer")"
    size="$(stat -c%s "$1")"
    local headers=(-H "X-Auth: $auth" -H "X-Auth-Rest: $rest" -H 'Tus-Resumable: 1.0.0')
    curl -sf -o /dev/null -X POST "$url/$2?override=true" "${headers[@]}" -H "Upload-Length: $size" -H 'Upload-Offset: 0' || return 1
    curl -sf -o /dev/null -X PATCH "$url/$2?override=true" "${headers[@]}" -H 'Upload-Offset: 0' \
         -H 'Content-Type: application/offset+octet-stream' --data-binary @"$1"
}

# A loud line when the newest published release's AppImage isn't what the address offers (the step that is easy to miss).
update_reminder()
{
    local newest current
    command -v gh > /dev/null || { echo "REMINDER: check that the AppImage update address is current: scripts/publish-appimage-update.sh v<label>"; return 0; }
    newest="$(gh api "repos/$UPDATE_REPO/releases?per_page=1" --jq '.[0].tag_name' 2> /dev/null)" || return 0
    [[ -n "$newest" && "$(printf '%s\n%s\n' "$UPDATE_FIRST_VERSION" "$newest" | sort -V | head -1)" == "$UPDATE_FIRST_VERSION" ]] || return 0
    current="$(update_target_tag "${1:-}")"
    if [[ "$current" != "$newest" ]]; then
        echo "=================================================================================="
        echo "REMINDER: the AppImage update address offers '${current:-nothing}', the newest release is $newest."
        echo "Linux update checks won't offer $newest until you run: scripts/publish-appimage-update.sh $newest"
        echo "=================================================================================="
    fi
}
