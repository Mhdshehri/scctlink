#!/bin/sh
# Build dist/SCCTLink-<ver>.zip. steam_api.dll must be the pinned file (pass its path).
set -eu
cd "$(dirname "$0")"
VER=$(sed -n 's/^#define VERSION "\(.*\)"/\1/p' src/scctlink.c)
DLL=${1:?usage: package.sh path/to/steam_api.dll}
echo "fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da  $DLL" | sha256sum -c -
sh build_windows.sh
D=dist/SCCTLink-$VER
rm -rf dist && mkdir -p "$D"
cp build/win/SCCTLink.exe "$DLL" package/Host-SCCT.bat package/Join-SCCT.bat "$D/"
for f in package/README.txt LICENSE; do sed "s/\r*$/\r/" "$f" > "$D/$(basename "$f")"; done  # Windows line endings
(cd "$D" && sha256sum SCCTLink.exe steam_api.dll Host-SCCT.bat Join-SCCT.bat README.txt LICENSE > SHA256SUMS.txt)
find "$D" -exec touch -d 2026-10-27T00:00:00Z {} +
(cd dist && TZ=UTC zip -X -q -r "SCCTLink-$VER.zip" "SCCTLink-$VER")
sha256sum "dist/SCCTLink-$VER.zip"
