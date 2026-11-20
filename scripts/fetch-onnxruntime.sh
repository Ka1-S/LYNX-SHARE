#!/usr/bin/env bash
# Puts ONNX Runtime's library for one platform into a folder, checksum verified. LYNX loads it at run time for
# "Extract stems from sample". 1.23.2 is the last release with a universal macOS build (Intel Macs are still supported).
#
# usage: scripts/fetch-onnxruntime.sh <windows|linux|macos> <output folder>
#
# Fallback (round 190nk, 2026-11-20: github.com/microsoft/onnxruntime answered 404 for the repository and its release
# downloads): when Microsoft's archive can't be downloaded, the same 1.23.2 library is taken from LYNX SHARE 0.1.29's
# release package (which this script filled from Microsoft's checksum-verified archive) and checked against the library's
# own SHA-256 below (Windows / Linux: Microsoft's file byte for byte, the same hash as in LYNX 0.1.24-0.1.29; macOS: the
# copy inside 0.1.29's app bundle, which package-macos.sh re-signs ad hoc anyway).
set -euo pipefail
PLATFORM="$1"; OUT="$2"
VERSION=1.23.2
case "$PLATFORM" in
    windows) ASSET="onnxruntime-win-x64-$VERSION.zip";        SHA=0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd; LIB=lib/onnxruntime.dll ;;
    linux)   ASSET="onnxruntime-linux-x64-$VERSION.tgz";      SHA=1fa4dcaef22f6f7d5cd81b28c2800414350c10116f5fdd46a2160082551c5f9b; LIB="lib/libonnxruntime.so.$VERSION" ;;
    macos)   ASSET="onnxruntime-osx-universal2-$VERSION.tgz"; SHA=49ae8e3a66ccb18d98ad3fe7f5906b6d7887df8a5edd40f49eb2b14e20885809; LIB="lib/libonnxruntime.$VERSION.dylib" ;;
    *) echo "unknown platform: $PLATFORM" >&2; exit 1 ;;
esac
FB_URL="https://github.com/Ka1-S/LYNX-SHARE/releases/download/v0.1.29-alpha"
case "$PLATFORM" in
    windows) FB_ASSET=LYNX-0.1.29-alpha-windows-x64.zip;    FB_LIB=LYNX-0.1.29-alpha-windows-x64/onnxruntime.dll
             FB_SHA=dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea ;;
    linux)   FB_ASSET=LYNX-0.1.29-alpha-linux-x86_64.tar.gz; FB_LIB="LYNX-0.1.29-alpha-linux-x86_64/libonnxruntime.so.$VERSION"
             FB_SHA=13ab8084954fa4a47c777880180b90810d6020f021441395712b48a75b74c68b ;;
    macos)   FB_ASSET=LYNX-0.1.29-alpha-macos-universal.dmg; FB_LIB="LYNX.app/Contents/Frameworks/libonnxruntime.$VERSION.dylib"
             FB_SHA=2e33c254d60eb81dbf3417a9f27d843cef8cd34344dbdd483e7bcd7d185ec909 ;;
esac
sha_ok() { if command -v sha256sum > /dev/null; then echo "$1  $2" | sha256sum -c -; else echo "$1  $2" | shasum -a 256 -c -; fi; }

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"
if ! curl -fsSL --retry 3 -o "$WORK/$ASSET" "https://github.com/microsoft/onnxruntime/releases/download/v$VERSION/$ASSET"; then
    echo "::warning::fetch-onnxruntime: Microsoft's $ASSET can't be downloaded; taking ONNX Runtime $VERSION from LYNX SHARE 0.1.29's $FB_ASSET"
    curl -fsSL --retry 3 -o "$WORK/$FB_ASSET" "$FB_URL/$FB_ASSET"
    lib="$WORK/$(basename "$FB_LIB")"
    case "$FB_ASSET" in
        *.zip)    python3 -c 'import sys, zipfile; open(sys.argv[3], "wb").write(zipfile.ZipFile(sys.argv[1]).read(sys.argv[2]))' \
                      "$WORK/$FB_ASSET" "$FB_LIB" "$lib" ;;
        *.tar.gz) tar xzf "$WORK/$FB_ASSET" -C "$WORK" "$FB_LIB" && mv "$WORK/$FB_LIB" "$lib" ;;
        *.dmg)    if command -v hdiutil > /dev/null; then
                      hdiutil attach -nobrowse -readonly -mountpoint "$WORK/mnt" "$WORK/$FB_ASSET" > /dev/null
                      cp "$WORK/mnt/$FB_LIB" "$lib"; hdiutil detach "$WORK/mnt" > /dev/null
                  else 7zz e -y -o"$WORK" "$WORK/$FB_ASSET" "LYNX SHARE 0.1.29-alpha/$FB_LIB" > /dev/null; fi ;;
    esac
    sha_ok "$FB_SHA" "$lib"
    install -m 755 "$lib" "$OUT/"
    exit 0
fi
sha_ok "$SHA" "$WORK/$ASSET"
case "$ASSET" in
    *.zip) python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$WORK/$ASSET" "$WORK" ;;
    *)     tar xzf "$WORK/$ASSET" -C "$WORK" ;;
esac
mkdir -p "$OUT"
install -m 755 "$WORK/${ASSET%.*}/$LIB" "$OUT/"
