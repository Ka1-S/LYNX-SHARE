#!/usr/bin/env bash
# Linux packages of a built LYNX binary: an AppImage (one file, runs on most distributions) and a .tar.gz
# (folder with install.sh for a per-user install). Both carry the .desktop entry, the icon and the .lynx file type.
#
# usage: scripts/package-linux.sh <LYNX binary> <label> <output folder> [Demo.lynx]
# Variables: APPIMAGETOOL (path to appimagetool; downloaded into ~/opt when missing), ARCH (default x86_64)
set -euo pipefail
cd "$(dirname "$0")/.."

BIN="$1"; LABEL="$2"; OUTDIR="$3"; DEMO="${4:-}"
ARCH="${ARCH:-x86_64}"
NAME="LYNX-$LABEL-linux-$ARCH"            # .tar.gz
APPIMAGE="LYNX-$LABEL-$ARCH.AppImage"     # AppImage names leave out "linux" (all AppImages are for Linux)
APPID=com.lynx_share.LYNX                 # AppStream component id = desktop file name inside the AppImage
APPIMAGETOOL_VERSION=1.9.0
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUTDIR"

# --- folder for the .tar.gz --------------------------------------------------------------------------------
PKG="$WORK/$NAME"
mkdir -p "$PKG"
install -m 755 "$BIN" "$PKG/LYNX"
strip --strip-unneeded "$PKG/LYNX" 2> /dev/null || true
./scripts/fetch-onnxruntime.sh linux "$PKG"
install -m 644 resources/linux/lynx.desktop resources/linux/lynx-mime.xml LICENSE THIRD_PARTY.md PRIVACY.md "$PKG/"
install -m 644 resources/lynx_icon_256.png "$PKG/lynx.png"
cp -r resources/linux/icons "$PKG/icons"     # hicolor 16-512 px (tools/make_logo.py; 16 and 24 px: the small-size form)
install -m 755 resources/linux/install.sh "$PKG/install.sh"
[[ -n "$DEMO" && -f "$DEMO" ]] && install -m 644 "$DEMO" "$PKG/Demo.lynx"
tar -C "$WORK" -czf "$OUTDIR/$NAME.tar.gz" "$NAME"

# --- AppImage ----------------------------------------------------------------------------------------------
APPDIR="$WORK/LYNX.AppDir"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons" \
    "$APPDIR/usr/share/mime/packages" "$APPDIR/usr/share/metainfo"
install -m 755 "$PKG/LYNX" "$PKG"/libonnxruntime.so.* "$APPDIR/usr/bin/"
install -m 644 resources/linux/lynx.desktop "$APPDIR/usr/share/applications/$APPID.desktop"
install -m 644 resources/linux/lynx.desktop "$APPDIR/$APPID.desktop"
# AppStream metadata with this build's version and date (validated by appimagetool when appstreamcli is installed)
RELDATE="$(git log -1 --format=%cs 2> /dev/null || date -u +%F)"
sed -e "s/@VERSION@/$LABEL/" -e "s/@DATE@/$RELDATE/" resources/linux/$APPID.appdata.xml > "$APPDIR/usr/share/metainfo/$APPID.appdata.xml"
cp -r resources/linux/icons/hicolor "$APPDIR/usr/share/icons/hicolor"
install -m 644 resources/lynx_icon_256.png "$APPDIR/lynx.png"
ln -sf lynx.png "$APPDIR/.DirIcon"
install -m 644 resources/linux/lynx-mime.xml "$APPDIR/usr/share/mime/packages/lynx-mime.xml"
for f in LICENSE THIRD_PARTY.md PRIVACY.md; do install -m 644 "$f" "$APPDIR/usr/share/$f"; done
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
exec "$here/usr/bin/LYNX" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

TOOL="${APPIMAGETOOL:-$HOME/opt/appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage}"
if [[ ! -x "$TOOL" ]]; then
    mkdir -p "$(dirname "$TOOL")"
    curl -fsSL -o "$TOOL" "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage"
    chmod +x "$TOOL"
fi
# no FUSE needed to run the tool itself (containers, CI)
# update information (Gear Lever, AppImageUpdate): a fixed address on lynx-share.com, no GitHub API (issue #48: its
# anonymous limit of 60 requests an hour shows up as "no updates"). scripts/publish-appimage-update.sh points that address
# at a published release's .zsync. Its URL line is absolute, because readers resolve a relative one against the fixed address.
UPDATE_INFO="${UPDATE_INFO-zsync|https://lynx-share.com/update/LYNX-$ARCH.AppImage.zsync}"
APPIMAGE_URL="${APPIMAGE_URL-https://github.com/Ka1-S/LYNX-SHARE/releases/download/v$LABEL/$APPIMAGE}"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$ARCH" "$TOOL" ${UPDATE_INFO:+-u "$UPDATE_INFO"} "$APPDIR" "$OUTDIR/$APPIMAGE" \
    > "$WORK/appimagetool.log" 2>&1 || { cat "$WORK/appimagetool.log"; exit 1; }
if [[ -n "$UPDATE_INFO" && ! -f "$OUTDIR/$APPIMAGE.zsync" ]]; then
    [[ -f "$APPIMAGE.zsync" ]] && mv "$APPIMAGE.zsync" "$OUTDIR/"
    # appimagetool only writes the .zsync when zsyncmake is installed (the release job installs it)
    [[ -f "$OUTDIR/$APPIMAGE.zsync" ]] || ! command -v zsyncmake > /dev/null \
        || { echo "no $APPIMAGE.zsync written"; cat "$WORK/appimagetool.log"; exit 1; }
fi
chmod 755 "$OUTDIR/$APPIMAGE"
if [[ -n "$UPDATE_INFO" && -f "$OUTDIR/$APPIMAGE.zsync" ]]; then
    ( cd "$OUTDIR" && zsyncmake -u "$APPIMAGE_URL" -o "$APPIMAGE.zsync" "$APPIMAGE" )
fi
# issue #48 (Gear Lever, AppImageUpdate): what they read must stay right. The embedded update information is ours, the
# .zsync points at this release's AppImage, and its name still matches the pattern embedded up to 0.1.24, which those
# versions use to find it on the releases.
if [[ -n "$UPDATE_INFO" ]]; then
    if command -v readelf > /dev/null; then
        embedded="$(readelf --string-dump=.upd_info --wide "$OUTDIR/$APPIMAGE" | sed -n 's/^ *\[ *0\] *//p' | sed 's/ *$//')"
        [[ "$embedded" == "$UPDATE_INFO" ]] || { echo "update information is '$embedded', not '$UPDATE_INFO'"; exit 1; }
    fi
    case "$APPIMAGE.zsync" in LYNX-*-$ARCH.AppImage.zsync) ;; *) echo "$APPIMAGE.zsync does not match LYNX-*-$ARCH.AppImage.zsync"; exit 1 ;; esac
    if [[ -f "$OUTDIR/$APPIMAGE.zsync" ]]; then
        url="$(head -c 4096 "$OUTDIR/$APPIMAGE.zsync" | sed -n 's/^URL: //p' | head -1)"
        [[ "$url" == "$APPIMAGE_URL" ]] || { echo "$APPIMAGE.zsync points at '$url'"; exit 1; }
    fi
fi
ls -la "$OUTDIR/$NAME.tar.gz" "$OUTDIR/$APPIMAGE"*
