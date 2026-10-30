#!/bin/sh
# Build dist/SCCTLink-<ver>.zip. steam_api.dll must be the pinned file (pass its path).
set -eu
cd "$(dirname "$0")"
VER=$(sed -n 's/^#define VERSION "\(.*\)"/\1/p' src/scctlink.c)
DLL=${1:?usage: package.sh path/to/steam_api.dll}
echo "fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da  $DLL" | sha256sum -c -
FIX=package/SCCTFix-d3d9.dll   # the FPS fix (SkyTheKraKen's d3d9.dll), pinned
echo "2f05d8e9e7aaf750eb2889c7ba57dd453820e3a794b9ba74c43730446a84cae9  $FIX" | sha256sum -c -
sh build_windows.sh
D=dist/SCCTLink-$VER
rm -rf dist && mkdir -p "$D"
cp build/win/SCCTLink.exe "$DLL" "$FIX" "$D/"
sed "s/\r*$/\r/" package/README.txt > "$D/README.txt"; sed "s/\r*$/\r/" LICENSE > "$D/LICENSE.txt"  # Windows line endings
(cd "$D" && sha256sum SCCTLink.exe steam_api.dll SCCTFix-d3d9.dll README.txt LICENSE.txt > SHA256SUMS.txt)
find "$D" -exec touch -d 2026-10-27T00:00:00Z {} +
(cd dist && TZ=UTC zip -X -q -r "SCCTLink-$VER.zip" "SCCTLink-$VER")
sha256sum "dist/SCCTLink-$VER.zip"
