#!/usr/bin/env bash
# Puts ONNX Runtime's library for one platform into a folder, checksum verified. LYNX loads it at run time for
# "Extract stems from sample". 1.23.2 is the last release with a universal macOS build (Intel Macs are still supported).
#
# usage: scripts/fetch-onnxruntime.sh <windows|linux|macos> <output folder>
set -euo pipefail
PLATFORM="$1"; OUT="$2"
VERSION=1.23.2
case "$PLATFORM" in
    windows) ASSET="onnxruntime-win-x64-$VERSION.zip";        SHA=0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd; LIB=lib/onnxruntime.dll ;;
    linux)   ASSET="onnxruntime-linux-x64-$VERSION.tgz";      SHA=1fa4dcaef22f6f7d5cd81b28c2800414350c10116f5fdd46a2160082551c5f9b; LIB="lib/libonnxruntime.so.$VERSION" ;;
    macos)   ASSET="onnxruntime-osx-universal2-$VERSION.tgz"; SHA=49ae8e3a66ccb18d98ad3fe7f5906b6d7887df8a5edd40f49eb2b14e20885809; LIB="lib/libonnxruntime.$VERSION.dylib" ;;
    *) echo "unknown platform: $PLATFORM" >&2; exit 1 ;;
esac

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
curl -fsSL -o "$WORK/$ASSET" "https://github.com/microsoft/onnxruntime/releases/download/v$VERSION/$ASSET"
if command -v sha256sum > /dev/null; then echo "$SHA  $WORK/$ASSET" | sha256sum -c -
else echo "$SHA  $WORK/$ASSET" | shasum -a 256 -c -; fi
case "$ASSET" in
    *.zip) python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$WORK/$ASSET" "$WORK" ;;
    *)     tar xzf "$WORK/$ASSET" -C "$WORK" ;;
esac
mkdir -p "$OUT"
install -m 755 "$WORK/${ASSET%.*}/$LIB" "$OUT/"
