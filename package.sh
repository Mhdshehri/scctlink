#!/bin/sh
# Build dist/SCCTLink-<ver>.zip. steam_api.dll must be the pinned file (pass its path).
# Optional second argument: the official wintun-0.14.1.zip (from wintun.net) for VPN mode.
set -eu
cd "$(dirname "$0")"
VER=$(sed -n 's/^#define VERSION "\(.*\)"/\1/p' src/scctlink.c)
DLL=${1:?usage: package.sh path/to/steam_api.dll [path/to/wintun-0.14.1.zip]}
WZ=${2:-}
echo "fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da  $DLL" | sha256sum -c -
if [ -n "$WZ" ]; then
    echo "07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51  $WZ" | sha256sum -c -
    rm -rf build/wintun && mkdir -p build/wintun && unzip -q -o "$WZ" -d build/wintun
    echo "d694fa46ab4cfebcb2632d094c7aa97278eef2f8052438621766d863ae98a931  build/wintun/wintun/bin/x86/wintun.dll" | sha256sum -c -
fi
sh build_windows.sh
D=dist/SCCTLink-$VER
rm -rf dist && mkdir -p "$D"
cp build/win/SCCTLink.exe "$DLL" "$D/"
sed "s/\r*$/\r/" package/README.txt > "$D/README.txt"; sed "s/\r*$/\r/" LICENSE > "$D/LICENSE.txt"  # Windows line endings
if [ -n "$WZ" ]; then
    cp build/wintun/wintun/bin/x86/wintun.dll "$D/"
    sed "s/\r*$/\r/" build/wintun/wintun/LICENSE.txt > "$D/wintun-LICENSE.txt"
    (cd "$D" && sha256sum SCCTLink.exe steam_api.dll wintun.dll README.txt LICENSE.txt wintun-LICENSE.txt > SHA256SUMS.txt)
else
    (cd "$D" && sha256sum SCCTLink.exe steam_api.dll README.txt LICENSE.txt > SHA256SUMS.txt)
fi
find "$D" -exec touch -d 2026-10-27T00:00:00Z {} +
(cd dist && TZ=UTC zip -X -q -r "SCCTLink-$VER.zip" "SCCTLink-$VER")
sha256sum "dist/SCCTLink-$VER.zip"
