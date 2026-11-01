#!/bin/sh
# Build the SCCT Fix (d3d9.dll, 32-bit) from fix/d3d9fix.c with the system's
# clang + lld only: no Windows SDK, no C runtime. The Windows functions it uses
# are listed below and turned into import libraries with llvm-dlltool.
# The same source and these flags give the same file every time.
set -eu
cd "$(dirname "$0")/.."
OUT=build/fix
mkdir -p "$OUT/lib"
mkdef() { # name, then exports
    name=$1; shift
    { echo "LIBRARY $name.dll"; echo "EXPORTS"; for e in "$@"; do echo "$e"; done; } > "$OUT/lib/$name.def"
    llvm-dlltool -m i386 -k -d "$OUT/lib/$name.def" -l "$OUT/lib/$name.lib"
}
mkdef kernel32 CreateFileA@28 DisableThreadLibraryCalls@4 GetModuleFileNameA@12 GetModuleHandleA@4 \
    GetProcAddress@8 GetSystemDirectoryA@8 GlobalFindAtomA@4 LoadLibraryA@4 QueryPerformanceCounter@4 \
    QueryPerformanceFrequency@4 SetUnhandledExceptionFilter@4 Sleep@4 VirtualProtect@16 WriteFile@20
mkdef user32 GetMonitorInfoA@8 GetWindowLongA@8 GetWindowRect@8 IsIconic@4 MonitorFromWindow@8 \
    RegisterHotKey@16 SetWindowLongA@12 SetWindowPos@28 ShowWindow@8 UnregisterHotKey@8 wsprintfA
mkdef winmm timeBeginPeriod@4

clang --target=i686-pc-windows-msvc -O2 -Wall -Wextra -fno-builtin -fno-stack-protector -ffreestanding \
    -c fix/d3d9fix.c -o "$OUT/d3d9fix.obj"
lld-link -dll -nodefaultlib -entry:DllEntry -safeseh:no -machine:x86 -subsystem:windows,6.0 \
    -export:Direct3DCreate9=_Direct3DCreate9@4 -Brepro -libpath:"$OUT/lib" \
    "$OUT/d3d9fix.obj" kernel32.lib user32.lib winmm.lib -out:"$OUT/d3d9.dll"
echo "built $OUT/d3d9.dll"
sha256sum "$OUT/d3d9.dll"
