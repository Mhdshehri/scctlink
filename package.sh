#!/bin/sh
# Build dist/SCCTLink-<ver>.zip. steam_api.dll must be the pinned file (pass its path).
set -eu
cd "$(dirname "$0")"
VER=$(sed -n 's/^#define SCCTLINK_VERSION "\(.*\)"/\1/p' src/ui.h)
DLL=${1:?usage: package.sh path/to/steam_api.dll}
echo "fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da  $DLL" | sha256sum -c -
# the SCCT Fix (d3d9.dll) and the Versus Fix (d3d8.dll) are built from fix/; each must be the file SCCTLink.exe pins
sh fix/build_fix.sh >/dev/null
FIX_SHA=$(sed -n 's/^#define SCCTFIX_SHA "\([0-9a-f]*\)".*/\1/p' src/scctlink.c)
echo "$FIX_SHA  build/fix/d3d9.dll" | sha256sum -c - || { echo "SCCTFIX_SHA in src/scctlink.c is not the built d3d9.dll"; exit 1; }
VFIX_SHA=$(sed -n 's/^#define VFIX_SHA "\(.*\)"/\1/p' src/scctlink.c)
echo "$VFIX_SHA  build/fix/d3d8.dll" | sha256sum -c - || { echo "VFIX_SHA in src/scctlink.c is not the built d3d8.dll"; exit 1; }
sh build_windows.sh
D=dist/SCCTLink-$VER
rm -rf dist && mkdir -p "$D"
cp build/win/SCCTLink.exe "$DLL" "$D/"
cp build/fix/d3d9.dll "$D/SCCTFix-d3d9.dll"
cp build/fix/d3d8.dll "$D/SCCTVersusFix-d3d8.dll"
sed "s/\r*$/\r/" package/README.txt > "$D/README.txt"; sed "s/\r*$/\r/" LICENSE > "$D/LICENSE.txt"; sed "s/\r*$/\r/" THIRD-PARTY.txt > "$D/THIRD-PARTY.txt"  # Windows line endings
(cd "$D" && sha256sum SCCTLink.exe steam_api.dll SCCTFix-d3d9.dll SCCTVersusFix-d3d8.dll README.txt LICENSE.txt THIRD-PARTY.txt > SHA256SUMS.txt)
find "$D" -exec touch -d 2026-10-27T00:00:00Z {} +
(cd dist && TZ=UTC zip -X -q -r "SCCTLink-$VER.zip" "SCCTLink-$VER")
sha256sum "dist/SCCTLink-$VER.zip"
