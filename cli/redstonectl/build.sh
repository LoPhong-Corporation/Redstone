#!/bin/sh
# Cross-compiles redstonectl for Windows, Linux and macOS into ./dist
set -e
cd "$(dirname "$0")"
mkdir -p dist
for target in windows/amd64 windows/arm64 linux/amd64 linux/arm64 darwin/amd64 darwin/arm64; do
  os=${target%/*}; arch=${target#*/}
  ext=""; [ "$os" = "windows" ] && ext=".exe"
  echo "building $os/$arch"
  CGO_ENABLED=0 GOOS=$os GOARCH=$arch go build -trimpath -ldflags "-s -w" -o "dist/redstonectl-$os-$arch$ext" .
done
