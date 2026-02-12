#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TP_DIR="$ROOT_DIR/third_party/termux-packages"
PKG_LIST="$ROOT_DIR/tools/termux-build/package-list.txt"
OUT_ASSETS="$ROOT_DIR/app/src/main/assets"
STAGE_DIR="$ROOT_DIR/tools/termux-build/stage"
ZIP_PATH="$OUT_ASSETS/termux-root.zip"

if [[ ! -d "$TP_DIR" ]]; then
  echo "termux-packages not found at $TP_DIR"
  exit 1
fi

cd "$TP_DIR"

./scripts/setup-ubuntu.sh

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

while IFS= read -r pkg; do
  if [[ -z "$pkg" || "$pkg" == \#* ]]; then
    continue
  fi
  ./build-package.sh -a aarch64 "$pkg"
  deb=$(ls -t output/*.deb | head -n1)
  if [[ -z "$deb" ]]; then
    echo "No .deb produced for $pkg"
    exit 1
  fi
  dpkg-deb -x "$deb" "$STAGE_DIR"
done < "$PKG_LIST"

mkdir -p "$OUT_ASSETS"
rm -f "$ZIP_PATH"
cd "$STAGE_DIR"
zip -r "$ZIP_PATH" .

echo "Created $ZIP_PATH"
