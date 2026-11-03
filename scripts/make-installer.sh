#!/usr/bin/env bash
# Packages a Windows build locally: the per-user installer (NSIS) and the portable zip. Official releases are
# packaged the same way by .github/workflows/release.yml (docs/RELEASE.md).
#
#   scripts/make-installer.sh [LYNX.exe] [Demo.lynx]
#
# Defaults: build-win/LYNX.exe; Demo.lynx is generated with `LYNX.exe --showcase` when not given (needs WSL
# interop; skipped otherwise). Output in dist/:
#   LYNX-<version>-setup-x64.exe          installer: %LOCALAPPDATA%\Programs\LYNX, shortcuts, .lynx, "Open with" for .flp
#   LYNX-<version>-windows-x64.zip        portable: extract and run LYNX.exe
#   SHA256SUMS.txt
#
# NSIS: $MAKENSIS, else makensis on PATH (Linux package), else the portable Windows NSIS in ~/opt/nsis-*
# (https://nsis.sourceforge.io, zip release: no install needed), run through WSL interop.
set -euo pipefail
cd "$(dirname "$0")/.."

EXE="${1:-build-win/LYNX.exe}"
DEMO="${2:-}"
VERSION="$(sed -n 's/^project(LYNX VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[[ -f "$EXE" ]] || { echo "no $EXE (run scripts/build-windows-zig.sh first)"; exit 1; }
[[ -n "$VERSION" ]] || { echo "version not found in CMakeLists.txt"; exit 1; }

DIST="dist"
NAME="LYNX-$VERSION-windows-x64"
STAGE="$DIST/stage/$NAME"
rm -rf "$DIST/stage"
mkdir -p "$STAGE"
cp "$EXE" "$STAGE/LYNX.exe"
[[ -f "$(dirname "$EXE")/onnxruntime.dll" ]] && cp "$(dirname "$EXE")/onnxruntime.dll" "$STAGE/"
cp LICENSE THIRD_PARTY.md "$STAGE/"
# LYNX ASIO driver (built next to LYNX.exe by build-windows-zig.sh) and its registration helpers
if [[ -f "$(dirname "$EXE")/lynx-asio.dll" ]]; then
    cp "$(dirname "$EXE")/lynx-asio.dll" "asio/Register LYNX ASIO.bat" "asio/Unregister LYNX ASIO.bat" "$STAGE/"
    [[ -f "$(dirname "$EXE")/lynx-asio32.dll" ]] && cp "$(dirname "$EXE")/lynx-asio32.dll" "$STAGE/"
fi
[[ -f PRIVACY.md ]] && cp PRIVACY.md "$STAGE/"

if [[ -n "$DEMO" ]]; then
    cp "$DEMO" "$STAGE/Demo.lynx"
elif command -v wslpath >/dev/null; then
    # LYNX's own demo project, made by the exe being packaged
    ( cd "$STAGE" && timeout 300 ./LYNX.exe --showcase Demo.lynx >/dev/null 2>&1 ) || true
    [[ -f "$STAGE/Demo.lynx" ]] || echo "warning: Demo.lynx not generated"
fi

# portable zip
rm -f "$DIST/$NAME.zip"
( cd "$DIST/stage" && python3 -c "import sys,zipfile,os
z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED)
for r,d,f in os.walk(sys.argv[2]):
    for x in sorted(f): z.write(os.path.join(r,x))
z.close()" "../$NAME.zip" "$NAME" )

# installer
SETUP="LYNX-$VERSION-setup-x64.exe"
MAKENSIS="${MAKENSIS:-$(command -v makensis || true)}"
if [[ -z "$MAKENSIS" ]]; then
    MAKENSIS="$(ls -d "$HOME"/opt/nsis-*/makensis.exe 2>/dev/null | sort -V | tail -1 || true)"
fi
[[ -n "$MAKENSIS" ]] || { echo "NSIS not found: install makensis or unzip the NSIS zip release into ~/opt"; exit 1; }

winpath() { if [[ "$MAKENSIS" == *.exe ]]; then wslpath -w "$1"; else echo "$1"; fi; }
ARGS=(-V2 -INPUTCHARSET UTF8 "-DVERSION=$VERSION" "-DSTAGE=$(winpath "$PWD/$STAGE")" "-DOUTFILE=$(winpath "$PWD/$DIST/$SETUP")"
      "-DICON=$(winpath "$PWD/resources/LYNX.ico")" "$(winpath "$PWD/resources/installer/LYNX.nsi")")
if [[ "$MAKENSIS" == *.exe ]]; then
    # Windows exe from WSL: run it from a Windows folder (a \\wsl$ working directory confuses cmd-based tools)
    ( cd /mnt/c && "$MAKENSIS" "${ARGS[@]}" )
else
    "$MAKENSIS" "${ARGS[@]}"
fi

( cd "$DIST" && sha256sum "$SETUP" "$NAME.zip" > SHA256SUMS.txt )
rm -rf "$DIST/stage"
ls -la "$DIST/$SETUP" "$DIST/$NAME.zip"
