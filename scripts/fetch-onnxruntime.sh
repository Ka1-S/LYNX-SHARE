#!/usr/bin/env bash
# Puts ONNX Runtime's library for one platform into a folder, checksum verified. LYNX loads it at run time for
# "Extract stems from sample". 1.23.2 is the last release with a universal macOS build (Intel Macs are still supported).
#
# usage: scripts/fetch-onnxruntime.sh <windows|linux|linux-aarch64|macos> <output folder>   (linux = x86_64)
#
# Sources, in order (round 190nk found github.com/microsoft/onnxruntime answering 404 on 2026-11-20; round 190nr saw the
# same later that day, repository and API included):
#   1. Microsoft's release archive on GitHub (archive SHA-256 pinned below).
#   2. Microsoft's NuGet package Microsoft.ML.OnnxRuntime 1.23.2 from nuget.org (package and library SHA-256 pinned).
#      Its win-x64 onnxruntime.dll and linux-x64 libonnxruntime.so are byte for byte the libraries of the GitHub
#      archives (the same hashes LYNX 0.1.24-0.1.30 shipped). It has no universal macOS library: the arm64 and x86_64
#      ones are joined with lipo (macOS only; package-macos.sh re-signs the bundle ad hoc anyway).
#   3. LYNX SHARE 0.1.30's release package, which holds the same 1.23.2 libraries (hashes checked: Windows / Linux =
#      Microsoft's files; macOS = the universal library 0.1.24-0.1.30 shipped).
# linux-aarch64 (Linux arm64, round 190oy) comes from the NuGet package only (its runtimes/linux-arm64 library, pinned
# below): GitHub's archive answered 404 when it was added, so its hash could not be pinned, and no LYNX release before
# 0.1.31 had an arm64 package to fall back on.
set -euo pipefail
PLATFORM="$1"; OUT="$2"
VERSION=1.23.2
case "$PLATFORM" in
    windows) ASSET="onnxruntime-win-x64-$VERSION.zip";        SHA=0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd; LIB=lib/onnxruntime.dll ;;
    linux)   ASSET="onnxruntime-linux-x64-$VERSION.tgz";      SHA=1fa4dcaef22f6f7d5cd81b28c2800414350c10116f5fdd46a2160082551c5f9b; LIB="lib/libonnxruntime.so.$VERSION" ;;
    linux-aarch64) ASSET=; SHA=; LIB="lib/libonnxruntime.so.$VERSION" ;;
    macos)   ASSET="onnxruntime-osx-universal2-$VERSION.tgz"; SHA=49ae8e3a66ccb18d98ad3fe7f5906b6d7887df8a5edd40f49eb2b14e20885809; LIB="lib/libonnxruntime.$VERSION.dylib" ;;
    *) echo "unknown platform: $PLATFORM" >&2; exit 1 ;;
esac
NUGET_URL="https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/$VERSION/microsoft.ml.onnxruntime.$VERSION.nupkg"
NUGET_SHA=25fe172f7fcdf34f2b5b02c8b997f6b88abc282956849d4af4335af23d0c4e4a
case "$PLATFORM" in
    windows) NUGET_LIBS="runtimes/win-x64/native/onnxruntime.dll=dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea" ;;
    linux)   NUGET_LIBS="runtimes/linux-x64/native/libonnxruntime.so=13ab8084954fa4a47c777880180b90810d6020f021441395712b48a75b74c68b" ;;
    linux-aarch64) NUGET_LIBS="runtimes/linux-arm64/native/libonnxruntime.so=648ffa64fbe027ae27139109410900cf776a030dec2dbbac51053318cc44c286" ;;
    macos)   NUGET_LIBS="runtimes/osx-arm64/native/libonnxruntime.dylib=d306d2bc768540766c7ed8a1e0ff05d2870c77a934ebeee4a7bafa1b732ef299
runtimes/osx-x64/native/libonnxruntime.dylib=8c9c78de65ea3786f987c0d980e9c1b13a3a5fbc6b3e2965ba05b450e6e4c054" ;;
esac
FB_VER=0.1.30-alpha
FB_URL="https://github.com/Ka1-S/LYNX-SHARE/releases/download/v$FB_VER"
case "$PLATFORM" in
    windows) FB_ASSET=LYNX-$FB_VER-windows-x64.zip;    FB_LIB=LYNX-$FB_VER-windows-x64/onnxruntime.dll
             FB_SHA=dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea ;;
    linux)   FB_ASSET=LYNX-$FB_VER-linux-x86_64.tar.gz; FB_LIB="LYNX-$FB_VER-linux-x86_64/libonnxruntime.so.$VERSION"
             FB_SHA=13ab8084954fa4a47c777880180b90810d6020f021441395712b48a75b74c68b ;;
    macos)   FB_ASSET=LYNX-$FB_VER-macos-universal.dmg; FB_LIB="LYNX.app/Contents/Frameworks/libonnxruntime.$VERSION.dylib"
             FB_SHA=2e33c254d60eb81dbf3417a9f27d843cef8cd34344dbdd483e7bcd7d185ec909 ;;
esac
sha_ok() { if command -v sha256sum > /dev/null; then echo "$1  $2" | sha256sum -c -; else echo "$1  $2" | shasum -a 256 -c -; fi; }

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"
# The library's file name inside LYNX's package (what src/ui/OnnxRuntime.h loads).
DEST="$(basename "$LIB")"
# 2. Microsoft's NuGet package: prints "ok" and installs when it worked; any failure falls through to 3.
from_nuget() {
    curl -fsSL --retry 3 -o "$WORK/ort.nupkg" "$NUGET_URL" || return 1
    sha_ok "$NUGET_SHA" "$WORK/ort.nupkg" || return 1
    local parts=() entry name sha
    while IFS= read -r entry; do
        name="${entry%%=*}"; sha="${entry#*=}"
        python3 -c 'import sys, zipfile; open(sys.argv[3], "wb").write(zipfile.ZipFile(sys.argv[1]).read(sys.argv[2]))' \
            "$WORK/ort.nupkg" "$name" "$WORK/part${#parts[@]}" || return 1
        sha_ok "$sha" "$WORK/part${#parts[@]}" || return 1
        parts+=("$WORK/part${#parts[@]}")
    done <<< "$NUGET_LIBS"
    if [ "${#parts[@]}" -gt 1 ]; then
        command -v lipo > /dev/null || { echo "fetch-onnxruntime: no lipo here to join NuGet's macOS libraries"; return 1; }
        lipo -create "${parts[@]}" -output "$WORK/$DEST" || return 1
    else
        mv "${parts[0]}" "$WORK/$DEST"
    fi
    install -m 755 "$WORK/$DEST" "$OUT/"
}
if [ -z "$ASSET" ]; then
    from_nuget && exit 0
    echo "fetch-onnxruntime: Microsoft's NuGet package of ONNX Runtime $VERSION can't be downloaded or checked" >&2
    exit 1
fi
if ! curl -fsSL --retry 3 -o "$WORK/$ASSET" "https://github.com/microsoft/onnxruntime/releases/download/v$VERSION/$ASSET"; then
    echo "::warning::fetch-onnxruntime: Microsoft's $ASSET can't be downloaded from GitHub; trying Microsoft's NuGet package"
    if [ -z "${LYNX_ORT_SKIP_NUGET:-}" ] && from_nuget; then exit 0; fi
    echo "::warning::fetch-onnxruntime: taking ONNX Runtime $VERSION from LYNX SHARE $FB_VER's $FB_ASSET"
    curl -fsSL --retry 3 -o "$WORK/$FB_ASSET" "$FB_URL/$FB_ASSET"
    lib="$WORK/$(basename "$FB_LIB")"
    case "$FB_ASSET" in
        *.zip)    python3 -c 'import sys, zipfile; open(sys.argv[3], "wb").write(zipfile.ZipFile(sys.argv[1]).read(sys.argv[2]))' \
                      "$WORK/$FB_ASSET" "$FB_LIB" "$lib" ;;
        *.tar.gz) tar xzf "$WORK/$FB_ASSET" -C "$WORK" "$FB_LIB" && mv "$WORK/$FB_LIB" "$lib" ;;
        *.dmg)    if command -v hdiutil > /dev/null; then
                      hdiutil attach -nobrowse -readonly -mountpoint "$WORK/mnt" "$WORK/$FB_ASSET" > /dev/null
                      cp "$WORK/mnt/$FB_LIB" "$lib"; hdiutil detach "$WORK/mnt" > /dev/null
                  else 7zz e -y -o"$WORK" "$WORK/$FB_ASSET" "LYNX SHARE $FB_VER/$FB_LIB" > /dev/null; fi ;;
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
